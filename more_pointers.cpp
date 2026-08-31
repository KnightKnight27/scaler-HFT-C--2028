#include<iostream>

struct Child{
	int id;
	int arr[4];
};

void swap(Child *a,Child *b){
        int *temp = (*a).arr;
	
	//(*a).arr = (*b).arr;	
	//(*b).arr = temp; 
}
void print(Child obj){
	std::cout << "id " << obj.id << std::endl;
	for(size_t i=0;i<4;i++){
		std::cout << obj.arr[i] <<" ";
	}
	std::cout << std::endl;
}
int main(){
	Child a{1,{255,2,3,4}};
	Child b{2,{1,1,1,0}};
	std::cout << a.arr[0] << std::endl;
	//print(a);
	//print(b);
	swap(&a,&b);
	//print(a);
	//print(b);
}
