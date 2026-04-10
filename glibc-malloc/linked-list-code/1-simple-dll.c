#include <stdio.h>
#include <stdlib.h>

struct node {
  int data;
  struct node* next;
  struct node* prev;
};
typedef struct node Node;

struct list{
  Node* head;
  Node* tail;
};
typedef struct list List;

Node* createNode(int initVal){
  Node* n = malloc(sizeof(Node));
  if (!n) return NULL;

  n->data = initVal;
  n->next = NULL;
  n->prev = NULL;
  return n;
}

int pushAtTail(List* lptr, int initVal){
  Node *new_node = createNode(initVal);
  if (!new_node){ return -1;}

  /* Check empty list. */
  if ((lptr->head == NULL) && (lptr->tail == NULL)){
    lptr->head = new_node;
    lptr->tail = new_node;
    return 0;
  }

  /* List with one or more nodes. */

  // Step1: Setup the new_node's links.
  new_node->prev = lptr->tail;
  new_node->next = lptr->head;

  // Step2: Modify the tail node and the tail pointer.
  (lptr->tail)->next = new_node;
  lptr->tail = new_node;

  // Step3: Modify the head node's prev link.
  (lptr->head)->prev = lptr->tail;

  return 0;
}

int pushAtHead(List* lptr, int initVal){
  Node* new_node = createNode(initVal);
  if (!new_node) return -1;

  /* Check empty list. */
  if ((lptr->head == NULL) && (lptr->tail == NULL)){
    lptr->head = new_node;
    lptr->tail = new_node;
    return 0;
  }

  /* List with one or more nodes. */

  // Step1: Set the new node's links.
  new_node->next = lptr->head;
  new_node->prev = lptr->tail;

  // Step2: Modify the current lptr head and the head node.
  (lptr->head)->prev = new_node;
  lptr->head = new_node;

  // Step3: Modify the tail node's next.
  (lptr->tail)->next = new_node;
}

void displayFromHead(List* lptr){
  Node* tmp = lptr->head;
  int i = 0;
  while (tmp->next != lptr->head){
    printf("Node%d: %d\n", i, tmp->data);
    i++;
    tmp = tmp->next;
  }
  printf("Node%d: %d\n\n", i, tmp->data);
}

void displayFromTail(List* lptr){
  Node* tmp = lptr->tail;
  int i = 5;
  while (tmp->prev != lptr->tail){
    printf("Node%d: %d\n", i, tmp->data);
    i--;
    tmp = tmp->prev;
  }
  printf("Node%d: %d\n\n", i, tmp->data);
}

int deleteFromHead(List* lptr){
  /* Empty list check. */
  if (lptr->head == NULL){ return -1; }

  /* Single node list. */
  if (lptr->head == lptr->tail){
    free(lptr->head);
    lptr->head = NULL;
    lptr->tail = NULL;
    return 0;
  }

  /* List with more than one node. */
  Node* cur_head = lptr->head;

  // Step1: Modify the tail node's next.
  (lptr->tail)->next = cur_head->next;

  // Step2: Modify the second node's prev.
  (cur_head->next)->prev = lptr->tail;

  // Step3: Modify the list head ptr.
  lptr->head = cur_head->next;

  // Step4: free(cur_head)
  free(cur_head);
}

int deleteFromTail(List* lptr){
  /* Empty list check. */
  if (lptr->head == NULL){ return -1; }

  /* Single node list. */
  if (lptr->head == lptr->tail){
    free(lptr->tail);
    lptr->head = NULL;
    lptr->tail = NULL;
    return 0;
  }

  /* List with more than one node. */
  Node* cur_tail = lptr->tail;

  // Step1: Modify the head node's prev.
  (lptr->head)->prev = cur_tail->prev;

  // Step2: Modify the second last node's next.
  (cur_tail->prev)->next = lptr->head;

  // Step3: Modify the list tail ptr.
  lptr->tail = cur_tail->prev;

  // Step4: free(cur_tail)
  free(cur_tail);
}

int main(void){
  List L1 = {0};
  L1.head = L1.tail = NULL;

  pushAtTail(&L1, 10);
  pushAtTail(&L1, 20);
  pushAtTail(&L1, 30);
  pushAtTail(&L1, 40);
  pushAtTail(&L1, 50);
  pushAtTail(&L1, 60);
  displayFromHead(&L1);

  deleteFromHead(&L1);
  displayFromHead(&L1);

  deleteFromTail(&L1);
  displayFromHead(&L1);
}
