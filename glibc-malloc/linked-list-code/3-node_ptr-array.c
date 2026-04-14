#include <stdio.h>
#include <stdlib.h>

struct node {
  int data;
  struct node* next;
  struct node* prev;
};
typedef struct node Node;

Node* createNode(int initVal){
  Node* n = malloc(sizeof(Node));
  if (!n) return NULL;

  n->data = initVal;
  n->next = NULL;
  n->prev = NULL;
  return n;
}

int pushAtHead(Node** head, Node** tail, int initVal){
  Node* new_node = createNode(initVal);
  if (!new_node) return -1;

  /* Check empty list. */
  if ((*head == NULL) && (*tail == NULL)){
    *head = new_node;
    *tail = new_node;
    new_node->next = new_node;
    new_node->prev = new_node;
    return 0;
  }

  /* List with one or more nodes. */

  // Step1: Set the new node's links.
  new_node->next = *head;
  new_node->prev = *tail;

  // Step2: Modify the current lptr head and the head node.
  (*head)->prev = new_node;
  *head = new_node;

  // Step3: Modify the tail node's next.
  (*tail)->next = new_node;

  return 0;
}

int pushAtTail(Node** head, Node** tail, int initVal){
  Node *new_node = createNode(initVal);
  if (!new_node){ return -1;}

  /* Check empty list. */
  if ((*head == NULL) && (*tail == NULL)){
    *head = new_node;
    *tail = new_node;
    new_node->next = new_node;
    new_node->prev = new_node;
    return 0;
  }

  /* List with one or more nodes. */

  // Step1: Setup the new_node's links.
  new_node->prev = *tail;
  new_node->next = *head;

  // Step2: Modify the tail node and the tail pointer.
  (*tail)->next = new_node;
  *tail = new_node;

  // Step3: Modify the head node's prev link.
  (*head)->prev = *tail;

  return 0;
}

void displayFromHead(Node* head){
  Node* tmp = head;
  do {
    printf("NodeValue: %d\n", tmp->data);
    tmp = tmp->next;
  } while (tmp != head);
}

void displayFromTail(Node* tail){
  Node* tmp = tail;
  do {
    printf("NodeValue: %d\n", tmp->data);
    tmp = tmp->prev;
  } while (tmp != tail);
}

int deleteFromHead(Node* head, Node* tail){
  /* Empty list check. */
  if (head == NULL) return -1;

  /* Single node list. */
  if (head == tail){
    free(head);
    head = NULL;
    tail = NULL;
    return 0;
  }

  /* List with more than one node. */
  Node* cur_head = head;

  // Step1: Modify the tail node's next.
  (tail)->next = cur_head->next;

  // Step2: Modify the second node's prev.
  (cur_head->next)->prev = tail;

  // Step3: Modify the list head ptr.
  head = cur_head->next;

  // Step4: free(cur_head)
  free(cur_head);

  return 0;
}

int deleteFromTail(Node* head, Node* tail){
  /* Empty list check. */
  if (head == NULL){ return -1; }

  /* Single node list. */
  if (head == tail){
    free(tail);
    head = NULL;
    tail = NULL;
    return 0;
  }

  /* List with more than one node. */
  Node* cur_tail = tail;

  // Step1: Modify the head node's prev.
  (head)->prev = cur_tail->prev;

  // Step2: Modify the second last node's next.
  (cur_tail->prev)->next = head;

  // Step3: Modify the list tail ptr.
  tail = cur_tail->prev;

  // Step4: free(cur_tail)
  free(cur_tail);

  return 0;
}

int initListHeaders(Node** listHdrs, unsigned int listCount){
  if (!listHdrs)  return -1;
  for (unsigned int i=0; i<(listCount*2); i++){
    listHdrs[i] = NULL;
  }
  return 0;
}

int main(void){
  unsigned int listCount = 10;
  Node* listHeaders[listCount*2];
  initListHeaders(listHeaders, 10);

  pushAtHead(&listHeaders[0], &listHeaders[1], 5);
  pushAtTail(&listHeaders[0], &listHeaders[1], 6);
  displayFromHead(listHeaders[0]);
  displayFromTail(listHeaders[0]);
}
