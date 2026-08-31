#include<iostream>
struct Child{
        int id;
        int arr[4];
};

void swap(Child *a,Child *b){
// WE NEED TO SWAP ONLY ARR OF TWO OBJECTS
        // [a]-----> [OBJECT\]
	int temp[4];
	for(size_t i=0;i<4;i++){
		temp[i] = (*a).arr[i];
	}
	for(size_t i=0;i<4;i++){
		(*a).arr[i] = (*b).arr[i];
	}
	for(size_t i=0;i<4;i++){
		(*b).arr[i] = temp[i];
	}
}
void print(Child obj){
        std::cout << "id " << obj.id << std::endl;
        for(size_t i=0;i<4;i++){
                std::cout << obj.arr[i] <<" ";
        }
        std::cout << std::endl;
}
int main(){
        Child a{1,{1,2,3,4}};
        Child b{2,{1,1,1,0}};
        print(a);
        print(b);
        swap(&a,&b);
        print(a);
        print(b);
}

