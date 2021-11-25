#include "executive.h"
#include "busy_wait.h"
#include <iostream>

#include <ctime>
#include <cstdlib>

#include <fstream>
//Inizializzo l'executive con 5 task e un periodo pari a 4 e lascio l'unit duration pari a 10  come di default (non la indico)
Executive exec(5, 4);

void task0()
{
    busy_wait(8);
}

void task1()
{

	busy_wait(13);
   
}

void task2()
{

	busy_wait(5);
}

void task3()
{

	busy_wait(12);
	exec.ap_task_request();
}

void task4()
{

	busy_wait(8);
    
}

/* Nota: nel codice di uno o piu' task periodici e' lecito chiamare Executive::ap_task_request() */

void ap_task()
{
	busy_wait(2);
}

int main()
{
	busy_wait_init();
	exec.set_periodic_task(0, task0, 1); // tau_1
	exec.set_periodic_task(1, task1, 2); // tau_2
	exec.set_periodic_task(2, task2, 1); // tau_3,1
	exec.set_periodic_task(3, task3, 3); // tau_3,2
	exec.set_periodic_task(4, task4, 2); // tau_3,3
	/* ... */
	
	exec.set_aperiodic_task(ap_task, 5);
	
	exec.add_frame({0,1,2});  
	exec.add_frame({0,3});	  
	exec.add_frame({0,1});    
	exec.add_frame({0,1});	  
	exec.add_frame({0,1,4});  	
    
    //faccio aperiodic task
    
    
	/* ... */

                
	exec.run();

	
	return 0;
}