//////////////////////////////////////////////////////////////////////////////////

/* CE1007/CZ1007 Data Structures
Lab Test: Section A - Linked List Questions
Purpose: Implementing the required functions for Question 3 */

//////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>

//////////////////////////////////////////////////////////////////////////////////

typedef struct _listnode
{
	int item;
	struct _listnode *next;
} ListNode;			// You should not change the definition of ListNode

typedef struct _linkedlist
{
	int size;
	ListNode *head;
} LinkedList;			// You should not change the definition of LinkedList


//////////////////////// function prototypes /////////////////////////////////////

// You should not change the prototype of this function
void moveOddItemsToBack(LinkedList *ll);

void printList(LinkedList *ll);
void removeAllItems(LinkedList *ll);
ListNode * findNode(LinkedList *ll, int index);
int insertNode(LinkedList *ll, int index, int value);
int removeNode(LinkedList *ll, int index);

//////////////////////////// main() //////////////////////////////////////////////

int main()
{
	LinkedList ll;
	int c, i, j;
	c = 1;
	//Initialize the linked list 1 as an empty linked list
	ll.head = NULL;
	ll.size = 0;


	printf("1: Insert an integer to the linked list:\n");
	printf("2: Move all odd integers to the back of the linked list:\n");
	printf("0: Quit:\n");

	while (c != 0)
	{
		printf("Please input your choice(1/2/0): ");
		scanf("%d", &c);

		switch (c)
		{
		case 1:
			printf("Input an integer that you want to add to the linked list: ");
			scanf("%d", &i);
			j = insertNode(&ll, ll.size, i);
			printf("The resulting linked list is: ");
			printList(&ll);
			break;
		case 2:
			moveOddItemsToBack(&ll); // You need to code this function
			printf("The resulting linked list after moving odd integers to the back of the linked list is: ");
			printList(&ll);
			removeAllItems(&ll);
			break;
		case 0:
			removeAllItems(&ll);
			break;
		default:
			printf("Choice unknown;\n");
			break;
		}
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////////////

void moveOddItemsToBack(LinkedList *ll)
{
	// 짝수 링크드리스트와 홀수 링크드리스트 생성
	LinkedList *odd = malloc(sizeof(LinkedList));
	LinkedList *even = malloc(sizeof(LinkedList));

	// 짝수 노드와 홀수 노드, 현재 탐색중인 노드 생성 
	ListNode *oddNode, *evenNode, *nowNode;

	oddNode = malloc(sizeof(ListNode));
	evenNode = malloc(sizeof(ListNode));

	odd->head = oddNode;
	even->head = evenNode;

	nowNode = ll->head;
	
	// 현재 노드를 하나씩 순회하면서 탐색
	while (nowNode != NULL) {
		int num = nowNode->item;
		
		if (num % 2 == 0) { // 짝수
			// 다음 노드를 만들어서 이어두고, 값을 할당함
			evenNode->next = malloc(sizeof(ListNode)); // 이러면 메모리 누수가 발생하는 현상은 어떻게? 
			evenNode = evenNode->next;
			evenNode->item = nowNode->item; 
			evenNode->next = NULL; // 다음 값을 비워둠 (마지막 노드에 쓰레기 노드 출력 방지)
		} else {
			// 홀수
			oddNode->next = malloc(sizeof(ListNode));
			oddNode = oddNode->next;
			oddNode->item = nowNode->item;
			oddNode->next = NULL;
		}

		nowNode = nowNode->next;
	}

	// 짝수 노드의 맨 마지막에 홀수 노드의 시작을 붙임. 처음엔 쓰레기값이 있을 수도 있으니 next후 붙여줌
	evenNode->next = odd->head->next;
	
	ll->head = even->head->next;
	return;
}

void bestOddItemsToBack(LinkedList *ll) {
	ListNode *evenHead = NULL;
	ListNode *oddHead = NULL;
	ListNode *evenTail = NULL;
	ListNode *oddTail = NULL;

	ListNode *cur = ll->head;

	while (cur != NULL) {
		ListNode *next = cur->next; // 아직 next값을 가지고 있는 노드 연결

		cur->next = NULL; // 기존 연결을 끊어둠

		if (cur->item % 2 == 0) { // 짝수의 경우
			if (evenHead == NULL) {
				evenHead = cur;
				evenTail = cur;
			} else {
				evenTail->next = cur; //짝수 다음 노드를 cur 으로 지정한다. next값이 null이니 노드 하나만 연결된다.
				evenTail = cur;
			}
		}
		else { // 홀수의 경우
			if (oddHead == NULL) {
				oddHead = cur;
				oddTail = cur;
			} else {
				oddTail->next = cur;
				oddTail = cur; 
			}
		}

		cur = next;
	}

	if (evenHead == NULL)
    {
        // 전부 홀수
        ll->head = oddHead;
    }
    else
    {
        // 짝수 뒤에 홀수 연결
        evenTail->next = oddHead;
        ll->head = evenHead;
    }
}

///////////////////////////////////////////////////////////////////////////////////

void printList(LinkedList *ll){

	ListNode *cur;
	if (ll == NULL)
		return;
	cur = ll->head;

	if (cur == NULL)
		printf("Empty");
	while (cur != NULL)
	{
		printf("%d ", cur->item);
		cur = cur->next;
	}
	printf("\n");
}


void removeAllItems(LinkedList *ll)
{
	ListNode *cur = ll->head;
	ListNode *tmp;

	while (cur != NULL){
		tmp = cur->next;
		free(cur);
		cur = tmp;
	}
	ll->head = NULL;
	ll->size = 0;
}


ListNode *findNode(LinkedList *ll, int index){

	ListNode *temp;

	if (ll == NULL || index < 0 || index >= ll->size)
		return NULL;

	temp = ll->head;

	if (temp == NULL || index < 0)
		return NULL;

	while (index > 0){
		temp = temp->next;
		if (temp == NULL)
			return NULL;
		index--;
	}

	return temp;
}

int insertNode(LinkedList *ll, int index, int value){

	ListNode *pre, *cur;

	if (ll == NULL || index < 0 || index > ll->size + 1)
		return -1;

	// If empty list or inserting first node, need to update head pointer
	if (ll->head == NULL || index == 0){
		cur = ll->head;
		ll->head = malloc(sizeof(ListNode));
		ll->head->item = value;
		ll->head->next = cur;
		ll->size++;
		return 0;
	}


	// Find the nodes before and at the target position
	// Create a new node and reconnect the links
	if ((pre = findNode(ll, index - 1)) != NULL){
		cur = pre->next;
		pre->next = malloc(sizeof(ListNode));
		pre->next->item = value;
		pre->next->next = cur;
		ll->size++;
		return 0;
	}

	return -1;
}


int removeNode(LinkedList *ll, int index){

	ListNode *pre, *cur;

	// Highest index we can remove is size-1
	if (ll == NULL || index < 0 || index >= ll->size)
		return -1;

	// If removing first node, need to update head pointer
	if (index == 0){
		cur = ll->head->next;
		free(ll->head);
		ll->head = cur;
		ll->size--;

		return 0;
	}

	// Find the nodes before and after the target position
	// Free the target node and reconnect the links
	if ((pre = findNode(ll, index - 1)) != NULL){

		if (pre->next == NULL)
			return -1;

		cur = pre->next;
		pre->next = cur->next;
		free(cur);
		ll->size--;
		return 0;
	}

	return -1;
}
