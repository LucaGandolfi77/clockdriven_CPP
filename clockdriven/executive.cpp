

#include <cassert>
#include <iostream>
#include "executive.h"
#include <unistd.h>


#include <thread>
#include <list>
#include <random>  

#include "rt/priority.h"
#include "rt/affinity.h"

using namespace std;

std::chrono::steady_clock::time_point start;
Executive::Executive(size_t num_tasks, unsigned int frame_length, unsigned int unit_duration)
	: p_tasks(num_tasks), frame_length(frame_length), unit_time(unit_duration)
{
}

void Executive::set_periodic_task(size_t task_id, std::function<void()> periodic_task, unsigned int wcet)
{
	assert(task_id < p_tasks.size()); // Fallisce in caso di task_id non corretto (fuori range)
	
	p_tasks[task_id].function = periodic_task;
	p_tasks[task_id].wcet = wcet;
	p_tasks[task_id].stato = IDLE;
	p_tasks[task_id].id = task_id;
}

void Executive::set_aperiodic_task(std::function<void()> aperiodic_task, unsigned int wcet)
{
 	ap_task.function = aperiodic_task;
 	ap_task.wcet = wcet;
	ap_task.stato = IDLE;
	ap_task.id = 128;
}
		
void Executive::add_frame(std::vector<size_t> frame)
{
	
	for (auto & id: frame)
		assert(id < p_tasks.size()); // Fallisce in caso di task_id non corretto (fuori range)
	
	frames.push_back(frame);

	
}

void Executive::setUpInitial()
{
	rt::affinity aff = 1;


	for (size_t id = 0; id < p_tasks.size(); ++id)
	{
		assert(p_tasks[id].function); // Fallisce se set_periodic_task() non e' stato invocato per questo id

		//Preparo i vari thread da mettere in esecuzione
		p_tasks[id].thread = std::thread(&Executive::task_function, std::ref(p_tasks[id]));
		rt::set_affinity(p_tasks[id].thread,aff);
		
	}

	//Imposto la priorità del task aperiodico al massimo possibile dopo l'executive
	rt::priority prioAp(rt::priority::rt_min);
	ap_task.thread = std::thread(&Executive::task_function, std::ref(ap_task));
	rt::set_affinity(ap_task.thread,aff);
	rt::set_priority(ap_task.thread,prioAp);
}

void Executive::run()
{
	assert(ap_task.function); // Fallisce se set_aperiodic_task() non e' stato invocato

	setUpInitial();

	std::thread exec_thread(&Executive::exec_function, this);
	
	rt::priority prioEx(rt::priority::rt_max);
	
	rt::affinity aff = 1;

	rt::set_priority(exec_thread,prioEx);
	rt::set_affinity(exec_thread,aff);

	exec_thread.join();
	
	ap_task.thread.join();

	for (auto & pt: p_tasks)
		pt.thread.join();
		    
}


int getTimeFromStart()
{
	auto end = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
	return elapsed.count();
}

//Funzione per stimare la durata, in frame, di un task aperiodico pronto per la schedulazione
unsigned int Executive::ap_task_request()
{
	int numeroFrameCompletamento = 0;

	int i;
	mtx.lock();
	//Parto dal calcolo dello slack time disponibile dal prossimo frame nel quale il task aperiodico verrà schedulato
	int frame = (curFrame + 1) % frames.size();
	//Imposto la richiesta di task aperiodico a true
	isAptIdle = true;
	mtx.unlock();
	
	long unsigned int totalAvailableTime = 0;
	int availableTimeInFrame = 0;
	int totalExecutionTimePeriodic = 0;
	while(totalAvailableTime < ap_task.wcet){
		//Scorro tutti i task in ogni frame e sommo il tempo impiegato a completare ognuno nel caso peggiore
		availableTimeInFrame = 0;
		totalExecutionTimePeriodic = 0;
		for(i=0;i<frames[frame].size();i++){
			totalExecutionTimePeriodic += p_tasks[frames[frame][i]].wcet;
		}
		availableTimeInFrame = frame_length - totalExecutionTimePeriodic;
		totalAvailableTime += availableTimeInFrame;

		numeroFrameCompletamento ++;
		
		frame = (frame + 1) % frames.size();

	}
	return numeroFrameCompletamento;
}


void Executive::task_function(Executive::task_data & task)
{
	//Eseguo lock sul mutex del singolo task perchè sto leggendo le sue variabili di stato
	
	std::unique_lock<std::mutex> lock(task.mtx);   
 	while (true) {
		while (task.stato != READY)
		{
			task.cv.wait(lock);
		}

		task.stato = RUNNING;

		//Eseguo la funzione del task non in sezione critica in modo da rendere accessibili dall'executive tutte le varibili di stato relative al task
		cout << "Task "<< task.id << " partito"<< "["<<getTimeFromStart()<<"ms]"<< "\n";
		lock.unlock();

		task.function();

		lock.lock();
		cout << "Task "<< task.id << " terminato" <<"["<<getTimeFromStart()<<"ms]"<< "\n";
		//Al termine dell'esecuzione metto il task in attesa
		task.stato = IDLE;
  	}

}
void Executive::scheduleAperiodic(){
	ap_task.mtx.lock();
	if(ap_task.stato == IDLE ){
		ap_task.stato = READY;
		ap_task.cv.notify_one();
		
	}
	else if(ap_task.stato == RUNNING)
	{
		//Segnalo errore nella schedulazione del task aperiodico (deadline miss)
		cout << "Deadline miss del task aperiodico"<<"["<<getTimeFromStart()<<"ms]"<< "\n";
		
	}

	ap_task.mtx.unlock();
}

void Executive::exec_function()
{
	int frame_id = 0;
	long unsigned int i;

	
	std::list<std::thread> threads;
	
	auto prevTime = std::chrono::steady_clock::now();

	//rt::priority prio(rt::priority::rt_max);
	start = std::chrono::steady_clock::now();

	while (true)
	{
    	//std::cout << "It took me " << elapsed.count() << " milliseconds." << std::endl;
		cout << "FRAME STARTED"<< "["<<getTimeFromStart()<<"ms]"<< "\n";

		//Leggo lo stato delle varibili globali in sezione critica
		mtx.lock();
		frame_id = curFrame;
		mtx.unlock();
		
		//Controllo se ci sono task in frame overrun. Essi non possono essere fermati e dunque li lascio continuare oltre al frame corrente 
		
		for (auto & pt: p_tasks){
			
			pt.mtx.lock();
			if(pt.stato == RUNNING)
			{
				//Imposto al minimo (possibile) la priorità dei task aperiodici ancora in esecuzione
				rt::priority prioFrameOverrun(rt::priority::rt_max-1);
				
				cout << "Task " <<  pt.id << " overrun"<< "["<<getTimeFromStart()<<"ms]"<< "\n";

				rt::set_priority(pt.thread,prioFrameOverrun);
			
				
			}
			pt.mtx.unlock();
		}

		//Se ci sono rilascio  i job aperiodici
		bool aperiodicIdle = true;
		mtx.lock();
		aperiodicIdle = isAptIdle;
		mtx.unlock();
		if(aperiodicIdle){

			mtx.lock();
			isAptIdle = false;
			mtx.unlock();
			scheduleAperiodic();
		}

			//rilascio i task periodici in coda relativi al frame corrente e gli assegno le corrispondenti priorità
		for(i=0;i<frames[frame_id].size();i++){
			//Assegno la priorità a partire dal primo in ordine decrescente considerando che la priorità massima è riserva all'executive
			//E la priorita appena dopo la massima al task aperiodico 
			rt::priority prioCurTask(rt::priority::rt_max - 2 - i);

			//Entro in sezione critica per impostare la priorità dei task e modificare il loro stato
		
			//cout << "Prio cur task"cout << "Id del task: " << p_tasks[frames[frame_id][i]].thread.get_id() << "\n";
			
			p_tasks[frames[frame_id][i]].mtx.lock();
			if(p_tasks[frames[frame_id][i]].stato != RUNNING)
			{
				rt::set_priority(p_tasks[frames[frame_id][i]].thread,prioCurTask);
				p_tasks[frames[frame_id][i]].stato = READY;
				//Notifico al task tramite la variabile di condizione che è pronto per essere schedulato e lo metto in stato di pronto 
				p_tasks[frames[frame_id][i]].cv.notify_one();
			} 
			p_tasks[frames[frame_id][i]].mtx.unlock();	
		}

		//Attendo che tutti i task terminino la propria esecuzione tramite una sleep
		//sfrutto la sleep until in modo da tener conto anche del tempo di overhead dovuto all'esecuzione dell'executive
		std::this_thread::sleep_until(prevTime + std::chrono::milliseconds(frame_length * unit_time));
        prevTime = std::chrono::steady_clock::now();

		//Al termine dell'esecuzione dei task controllo il loro stato per verificare se ce ne sono ancora in running (frame overrun)
		//o alcuni task che dovevano andare in esecuzione non sono partiti 

		for(i=0;i<frames[frame_id].size();i++){
			
			//curTask = &(p_tasks[frames[frame_id][i]]);

			//Entro in sezione critica per leggere lo stato dei task
			p_tasks[frames[frame_id][i]].mtx.lock();
			if(p_tasks[frames[frame_id][i]].stato == READY){
				//notifico la mancata partenza di tale job ed inoltre cambio il suo stato in modo da non farlo piu' partire

				p_tasks[frames[frame_id][i]].stato = IDLE;
				cout << "Task " << p_tasks[frames[frame_id][i]].id << " non schedulato"<<"["<<getTimeFromStart()<<"ms]"<<"\n";
			}

			p_tasks[frames[frame_id][i]].mtx.unlock();
		}

		mtx.lock();
		if (++curFrame == frames.size())
			curFrame = 0;
		mtx.unlock();
	}
}


