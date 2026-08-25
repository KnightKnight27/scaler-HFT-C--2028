#include <iostream>
void swap(int *ptrX, int* ptrY){
	int temp = *ptrX; //10
	*ptrX = *ptrY;
        *ptrY = temp;	
}
int main(){
	int x = 10;
	int y = 20;
	std::cout << x << " " << y << std::endl;
	swap(&x,&y);
	std::cout << x <<" " << y << std::endl;
}
