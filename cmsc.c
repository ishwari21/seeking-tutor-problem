/* This program uses POSIX threads, mutex locks, and semaphores to implement a solution
   that synchronizes the activities of the students, coordinator, and tutors. 
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

int num_students;
int num_tutors;
int available_chairs;
int num_helps;

int waiting_students = 0;
int total_requests = 0;
int students_tutored_now = 0;
int sessions_tutored = 0;

struct student_info {
    int student_id;
    int helps_received;
    int arrived;
};
struct student_info *student;

typedef struct node {
    int student_id;
    int priority;
    struct node* next;
} node;

node* new_node(int id, int p) {
    node* temp = (node*) malloc(sizeof(node));
    temp->student_id = id;
    temp->priority = p;
    temp->next = NULL;
    return temp;
}

int peek(node** head) {
    return (*head)->student_id;
}

void pop(node** head) {
    node* temp = *head;
    (*head) = (*head)->next;
    free(temp);
}

void push(node** head, int id, int p) {
    node* start = (*head);
    node* temp = new_node(id, p);
    if ((*head)->priority > p) {
        temp->next = *head;
        (*head) = temp;
    }
    else {
        while (start->next != NULL && start->next->priority < p)
            start = start->next;
        temp->next = start->next;
        start->next = temp;
    }
}

int is_empty(node** head) {
    return (*head) == NULL;
}

int* tutor_ids;

pthread_mutex_t chair_lock;
sem_t students;
sem_t coordinator;
sem_t tutors;

void *run_students(void *student_id);
void *run_tutors(void *tutor_id);
void *run_coordinator();

int main(int argc, char *argv[]) {
    if (argc != 5)
        exit(1);

    num_students = atoi(argv[1]);
    num_tutors = atoi(argv[2]);
    available_chairs = atoi(argv[3]);
    num_helps = atoi(argv[4]);
    
    student = (struct student_info *)malloc(num_students * sizeof(struct student_info));
    if (student == NULL)
        exit(1);
    
    tutor_ids = (int*) malloc(num_tutors * sizeof(int));
    if (tutor_ids == NULL)
        exit(1);
    
    pthread_mutex_init(&chair_lock,NULL);

    pthread_t student_threads[num_students];
    pthread_t coordinator_thread;
    pthread_t tutor_threads[num_tutors];

    for (int i = 0; i < num_students; i++) {
        //(student+i)->student_id = i + 1;
        student[i].student_id = i + 1;
        student[i].helps_received = 0;
        student[i].arrived = 0;
        pthread_create(&student_threads[i], NULL, run_students, (void*) &student[i].student_id);
    }
    
    pthread_create(&coordinator_thread, NULL, run_coordinator, NULL);

    for (int i = 0; i < num_tutors; i++) {
        tutor_ids[i] = i + 1;
        pthread_create(&tutor_threads[i], NULL, run_tutors, (void*) &tutor_ids[i]);
    }
    
    for(int i = 0; i < num_students; i++)
        pthread_join(student_threads[i],NULL);
    
    pthread_join(coordinator_thread, NULL);

    for(int i = 0; i < num_tutors; i++)
        pthread_join(tutor_threads[i],NULL);
}

void *run_students(void *student_id) {
    int s_id = *((int*)student_id);
    
    while(1) {
        if (student[s_id-1].helps_received == num_helps) {
            num_students--;
            pthread_exit(NULL);
        }

        // rand() % 2 produces values between 0 and 2
        int sleep_time = rand() % 2;
        sleep_time /= 1000;
        // argument to sleep function is in seconds
        sleep(sleep_time);

        pthread_mutex_lock(&chair_lock);
        if (available_chairs == 0) {
            printf("S: Student %d found no empty chair. Will try again later.\n", s_id);
            pthread_mutex_unlock(&chair_lock);
            continue;
        }
        available_chairs--;
        waiting_students++;
        total_requests++;
        printf("S: Student %d takes a seat. Empty chairs = %d.\n", s_id, available_chairs);
        student[s_id-1].arrived = 1;
        pthread_mutex_unlock(&chair_lock);
        
        sem_post(&coordinator);
        sem_wait(&tutors);
        sleep(0.2/1000);
        
        pthread_mutex_lock(&chair_lock);
        available_chairs++;
        student[s_id-1].helps_received++;
        pthread_mutex_unlock(&chair_lock);
    }
}

void *run_coordinator() {
    int first_time = 1;
    node* add_student;
    while(1) {
        if (num_students == 0) {
            for(int i = 0; i < num_tutors; i++)
                sem_post(&tutors);
            pthread_exit(NULL);
        }
        sem_wait(&students);
        pthread_mutex_lock(&chair_lock);
        for(int i = 0; i < num_students; i++) {
            if (student[i].arrived == 1) {
                if (first_time == 1) {
                    add_student = new_node(student[i].student_id, num_helps - student[i].helps_received);
                    first_time = 0;
                }
                else 
                    push(&add_student, student[i].student_id, num_helps - student[i].helps_received);
                printf("C: Student %d with priority %d added to the queue. Waiting students now = %d. Total requests = %d.\n", 
                student[i].student_id, num_helps - student[i].helps_received, waiting_students, total_requests);
                student[i].arrived = 0;
                sem_post(&tutors);
            }
        }
        pthread_mutex_unlock(&chair_lock);
    }
    
}

void *run_tutors(void *tutor_id) {
    //int t_id = *((int*)tutor_id);
    while(1) {
        if (num_students == 0) 
            pthread_exit(NULL);
        sem_wait(&coordinator);

        sleep(0.2/1000);
    }
    
}