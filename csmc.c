/** This program uses POSIX threads, mutex locks, and semaphores to implement a solution
 * (to the seeking tutor problem) that synchronizes the activities of the students, coordinator, and tutors. 
 * The computer science department runs a mentoring center (csmc) to help undergraduate students with 
 * their programming assignments. The lab has a coordinator and tutor(s) to assist the student(s). 
 * 
 * Student implementation:
 * --Student is programming and decides to seek help from a tutor.
 * --Student arrives at the center and sits in an empty seat.
 *   --If student does not find an empty seat, student goes back to programming and comes back later.
 * --Once student gets a seat, student adds itself to a queue based on first come first serve (FCFS) basis.
 * --Student notifies coordinator and waits for tutor.
 * --After getting help, student will go back to programming.
 * --Once student gets the requested number of helps, student is done (terminates).
 * 
 * Coordinator implementation:
 * --Coordinator waits for students to come seek help.
 * --Coordinator takes students from the queue based on FCFS and queues them based on their priority.
 *   --The priority of a student is based on the number of times the student has taken help from a tutor. 
 *     A student visiting the center for the first time gets the highest priority. 
 *     In general, a student visiting to take help for the ith time has a priority higher than the priority 
 *     of the student visiting to take help for the kth time for any k > i. If two students have the same priority, 
 *     then the student who came first has a higher priority.
 * --Coordinator picks an idle tutor.
 * --If all students are done, then coordinator notifies the tutors and terminates.
 * 
 * Tutor implementation:
 * --Tutor waits for coordinator to notify that student has come to seek help.
 * --Tutor picks student with highest priority and tutors them.
 * --If all students are done, tutor waits for coordinator notification to terminate.
 *
 * Student IDs and Tutor IDs are assigned from 1 to the number of students and number of tutors, respectively.
 * Highest priority is 0 and lowest priority is the number of helps a student must receive from a tutor.
 *
 * Program is compiled as follows: gcc csmc.c -o csmc -Wall -Werror -pthread -std=gnu99
**/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>	// Library for pthread
#include <semaphore.h>	// Library for semaphore

int num_students;	// Number of students
int num_tutors;		// Number of tutors
int available_chairs;	// Number of available chairs
int num_helps;		// Number of times a student seeks a tutor's help

int waiting_students = 0;	// Number of waiting students
int total_requests = 0;		// Total number of requests (notifications sent) by students for tutoring
int students_tutored_now = 0;	// Number of students receiving help now
int sessions_tutored = 0;	// Total number of tutoring sessions completed so far by all the tutors
int students_done = 0;		// Number of students that received all helps

// Structure to store student information
struct student_info {
    int student_id;
    int helps_received;
    pthread_t thread;		// Thread for every student
    sem_t sem;			// Semaphore for every student
    struct tutor_info *my_tutor;	// Tutor stucture to store who tutored the student
};
struct student_info *student;	// Each student has a structure with their student information

// Structure to store tutor information
struct tutor_info {
    int tutor_id;
    int tutor_busy;		// 1 if tutor is busy and 0 otherwise 
    pthread_t thread;		// Thread for every tutor
    sem_t sem;			// Semaphore for every tutor
    struct student_info *my_student;	// Student structure to store which student is being tutored
    pthread_mutex_t tutor_lock;	// Mutex lock for every tutor
};
struct tutor_info *tutor;	// Each tutor has a structure with their tutor information

// Priority queue of students (using linked list)
// Each node in the queue stores the student id and their priority (the next node is used for pushing and popping from the queue) 
typedef struct node {
    int student_id;
    int priority;	// Lower values indicate higher priority
    struct node* next;
} node;
node* student_queue;	// Once student arrives, it adds itself to student_queue (based on FCFS)
node* priority_queue;	// Once student notifies coordinator, coordinator adds it to priority_queue (based on priority)

// Creates a new node using id and priority
node* new_node(int id, int p) {
    node* temp = (node*) malloc(sizeof(node));
    temp->student_id = id;
    temp->priority = p;
    temp->next = NULL;
    return temp;
}

// Returns the value at head (student id)
int peek(node** head) {
    return (*head)->student_id;
}

// Removes node with highest priority from head of the list
void pop(node** head) {
    node* temp = *head;
    (*head) = (*head)->next;
    free(temp);
}

// Pushes node into list based on priority
void push(node** head, int id, int p) {
    node* start = (*head);
    node* temp = new_node(id, p);
    
    // If head of list has less priority (higher value) than the new node
    // New node is placed before head and head is moved
    if ((*head)->priority > p) {
        temp->next = *head;
        (*head) = temp;
    }
    // Else go through list and find position for the new node
    // Students with equal priority are tutored in FCFS order
    else {
        while (start->next != NULL && start->next->priority <= p)
            start = start->next;
        temp->next = start->next;
        start->next = temp;
    }
}

// Returns 1 is list is empty and 0 otherwise
int is_empty(node** head) {
    return (*head) == NULL;
}

pthread_mutex_t chair_lock;	// Mutex lock for chair (one thread accesses chair at a time)
pthread_mutex_t queue_lock;	// Mutex lock for queue (one thread accesses queue at a time)
pthread_t coordinator_thread;	// Thread for coordinator
sem_t student_to_coordinator;	// Semaphore for coordinator

// Thread function declarations
void *run_students(void *);
void *run_tutors(void *);
void *run_coordinator();

int main(int argc, char *argv[]) {
    if (argc != 5)
        exit(1);
    
    // Program is run as follows: ./csmc #students #tutors #chairs #help
    num_students = atoi(argv[1]);
    num_tutors = atoi(argv[2]);
    available_chairs = atoi(argv[3]);
    num_helps = atoi(argv[4]);
    
    // Dynamically allocate memory for student stucture
    student = (struct student_info *) malloc(num_students * sizeof(struct student_info));
    // Check for memory allocation error
    if (student == NULL)
        exit(1);
    
    // Fill in student structure for every student
    for (int i = 0; i < num_students; i++) {
	student[i].student_id = i + 1;	// Id of first student is 1
	student[i].helps_received = 0;	// Initially each student has received no helps
	student[i].my_tutor = NULL;	// Initially tutor has not been assigned to student
	// Create student threads
	pthread_create(&student[i].thread, NULL, run_students, (void*) &student[i]);
        // Initialize student semaphores
	sem_init(&student[i].sem, 0, 0);
    }

    // Dynamically allocate memory for tutor structure
    tutor = (struct tutor_info *) malloc(num_tutors * sizeof(struct tutor_info));
    // Check for memory allocation error
    if (tutor == NULL)
        exit(1);
    
    // Fill in tutor structure for every tutor
    for (int i = 0; i < num_tutors; i++) {
	tutor[i].tutor_id = i + 1;	// Id of first tutor is 1
	tutor[i].tutor_busy = 0;	// Initially each tutor is not busy
        tutor[i].my_student = NULL;	// Initially student has not been assigned to tutor
	// Create tutor threads
	pthread_create(&tutor[i].thread, NULL, run_tutors, (void*) &tutor[i]);
	// Initialize tutor semaphores
	sem_init(&tutor[i].sem, 0, 0);
	// Initialize lock per tutor
	pthread_mutex_init(&tutor[i].tutor_lock, NULL);
    }
    
    // Initialize lock for using a chair
    pthread_mutex_init(&chair_lock,NULL);
    // Initialize lock for using the queue
    pthread_mutex_init(&queue_lock,NULL);
    // Create coordinator thread
    pthread_create(&coordinator_thread, NULL, run_coordinator, NULL);
    // Initialize coordinator semaphore
    sem_init(&student_to_coordinator, 0, 0);
    
    // Wait for all student threads to complete
    for(int i = 0; i < num_students; i++)
        pthread_join(student[i].thread,NULL);
    // Wait for coordinator thread to complete
    pthread_join(coordinator_thread, NULL);
    // Wait for all tutor threads to complete
    for(int i = 0; i < num_tutors; i++)
        pthread_join(tutor[i].thread,NULL);
    
    // Destroy all student semaphores once complete
    for(int i = 0; i < num_students; i++)
        sem_destroy(&student[i].sem);
    // Destroy coordinator semaphore once complete
    sem_destroy(&student_to_coordinator);
    // Destroy all tutor semaphore once complete
    for(int i = 0; i < num_tutors; i++)
        sem_destroy(&tutor[i].sem);
    
    // Free allocated memory
    free(student);
    free(tutor);
}

void *run_students(void *s) {
    // Each student thread receives its student structure (as argument to thread)
    struct student_info *s_info = (struct student_info *)s;

    while(1) {
    	pthread_mutex_lock(&chair_lock);
	// If student received all helps
        if (s_info->helps_received - num_helps >= 0) {
	    students_done++;	// Student is done
	    pthread_mutex_unlock(&chair_lock);
	    sem_post(&student_to_coordinator); // Notify coordinator
	    // Terminate student thread
	    pthread_exit(NULL);
        }
	pthread_mutex_unlock(&chair_lock);

        // Simulate programming by sleeping for a random amount of time up to 2 ms (2000 microseconds)
        float sleep_time = ((float)rand() / RAND_MAX) * 2000;
        // Argument to usleep function is in microseconds
        usleep(sleep_time);

        pthread_mutex_lock(&chair_lock);	// Lock chair
        if (available_chairs <= 0) {		// If no empty chairs
	    printf("S: Student %d found no empty chair. Will try again later.\n", s_info->student_id);
            pthread_mutex_unlock(&chair_lock);	// Unlock chair
	    continue;				// Go back to programming
        }
	available_chairs--;	// Decrement available chairs
        waiting_students++;	// Increment waiting students
        total_requests++;	// Increment total requests
        printf("S: Student %d takes a seat. Empty chairs = %d.\n", s_info->student_id, available_chairs);
        pthread_mutex_lock(&queue_lock);	// Lock queue
	// Student adds itself to queue based on who came first
	// Assumes equal priority for all students (coordinator will sort based on priority later)
	// If list is empty, create new node
	if (is_empty(&student_queue)) {
            student_queue = new_node(s_info->student_id, 0);
        }
	// Else push student into list
        else {
            push(&student_queue, s_info->student_id, 0);
        }
	pthread_mutex_unlock(&queue_lock);	// Unlock queue
	pthread_mutex_unlock(&chair_lock);      // Unlock chair

	sem_post(&student_to_coordinator); 	// Wake up coordinator
	sem_wait(&s_info->sem);			// Student waits (goes to sleep) until tutor wakes them up
	
	// Once here, student has been woken up by tutor
	// Simulate tutoring by sleeping for 0.2 ms (200 microseconds)
	usleep(200);
        
	pthread_mutex_lock(&chair_lock);
        s_info->helps_received++;	// Increment helps received by student
	printf("S: Student %d received help from Tutor %d.\n", s_info->student_id, s_info->my_tutor->tutor_id);
	s_info->my_tutor = NULL;	// Reset tutor to NULL
	pthread_mutex_unlock(&chair_lock);
    }
}

void *run_coordinator() {
    while(1) {
	sem_wait(&student_to_coordinator);	// Wait for student notification (that they have arrived)
	pthread_mutex_lock(&chair_lock);
	// If no students waiting in the queue
	if (is_empty(&student_queue)) { 
	    // If all students received all helps
	    if (students_done - num_students >= 0) { 
	        pthread_mutex_unlock(&chair_lock);
	        // Wake up all tutors
	        for(int i = 0; i < num_tutors; i++)
                   sem_post(&tutor[i].sem);
	        // Terminate coordinator thread
	        pthread_exit(NULL);
            }
	    // Queue is empty but there are still students to help. 
	    // Coordinator was woken up by a student that received all its helps
	    else {
                pthread_mutex_unlock(&chair_lock);
	        continue;	// Wait for the next student notification
	    }
	}
	pthread_mutex_unlock(&chair_lock);
		
	pthread_mutex_lock(&chair_lock);
	pthread_mutex_lock(&queue_lock);
	// Find out which student arrived and add them to priority queue
	int s_id = peek(&student_queue);	// Student who arrived first is at head of queue
	for (int i = 0; i < num_students; i++) {
	    if (student[i].student_id == s_id) { 
	        // If list is empty, create new node
		if (is_empty(&priority_queue)) {
		    priority_queue = new_node(student[i].student_id, student[i].helps_received);
		}
		// Else push student into list
		else {
		    push(&priority_queue, student[i].student_id, student[i].helps_received);
	        }
	        printf("C: Student %d with priority %d added to the queue. Waiting students now = %d. Total requests = %d.\n", student[i].student_id, student[i].helps_received, waiting_students, total_requests);
		pop(&student_queue);	// Remove student from student queue
		
		// Find idle tutor and wake them up
		int posted = 0; 
		for (int j = 0; j < num_tutors; j++) {
	     	    if (tutor[j].tutor_busy == 0) { // Tutor not busy
                 	posted = 1;
		 	sem_post(&tutor[j].sem); // Wake up tutor
                 	break;
	     	    }
        	}
		// if posted is still 0 (tutor has not been assigned after looping through all tutors), then wake up Tutor 1
		if (posted == 0) sem_post(&tutor[0].sem);
		break;
	    }
        }
	pthread_mutex_unlock(&queue_lock);
	pthread_mutex_unlock(&chair_lock);
    }
}

void *run_tutors(void *t) {
    // Each tutor thread receives its tutor structure (as argument to thread)
    struct tutor_info *t_info = (struct tutor_info *)t;

    while(1) {
        sem_wait(&t_info->sem);		// Wait for coordinator notification (to tutor student)
	pthread_mutex_lock(&chair_lock);
	// If no students waiting in the queue
	if (is_empty(&priority_queue)) {
	    // If all students received all helps, then terminate tutor thread
	    if (students_done - num_students >= 0) {
	        pthread_mutex_unlock(&chair_lock);
	        pthread_exit(NULL);
	    }
	}
	pthread_mutex_unlock(&chair_lock);

	pthread_mutex_lock(&t_info->tutor_lock); // Lock each tutor
	pthread_mutex_lock(&queue_lock);	 // Lock queue
	t_info->tutor_busy = 1;			 // Update tutor busy to 1
	int s_id = peek(&priority_queue);	 // Student with highest priority is at head of list 
	// Find out which student to assign to tutor
	for (int i = 0; i < num_students; i++) {
	    if (student[i].student_id == s_id) {
		t_info->my_student = &student[i];
            }
        }
	t_info->my_student->my_tutor = t_info;	// Copy tutor information so student knows who tutored them
	pop(&priority_queue);	// Remove student from priority queue
	pthread_mutex_unlock(&queue_lock);	// Unlock queue
	pthread_mutex_unlock(&t_info->tutor_lock); // Unlock tutor that acquired lock
	
	pthread_mutex_lock(&t_info->tutor_lock);
	pthread_mutex_lock(&chair_lock);
	students_tutored_now++;	// Increment students being tutored now
        available_chairs++;   	// Increment available chairs (leaving waiting area)
	waiting_students--;   	// Decrement waiting students (student is no longer waiting)
	pthread_mutex_unlock(&chair_lock);
	pthread_mutex_unlock(&t_info->tutor_lock);
	
	sem_post(&t_info->my_student->sem);	// Wake up student for tutoring
	
	// Simulate tutoring by sleeping for 0.2 ms (200 microseconds)
	// Once student is woken, it also sleeps for the same amount of time
	usleep(200);
	
	pthread_mutex_lock(&t_info->tutor_lock);
	pthread_mutex_lock(&chair_lock);
	sessions_tutored++;	// Increment sessions tutored
	printf("T: Student %d tutored by Tutor %d. Students tutored now = %d. Total sessions tutored = %d.\n", t_info->my_student->student_id, t_info->tutor_id, students_tutored_now, sessions_tutored);
	students_tutored_now--;	// Decrement students being tutored now
   	pthread_mutex_unlock(&chair_lock);
	
	t_info->tutor_busy = 0;	// Reset tutor busy to 0
        t_info->my_student = NULL;	// Reset student to NULL 
        pthread_mutex_unlock(&t_info->tutor_lock);
   }   
}
