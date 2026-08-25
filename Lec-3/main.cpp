#include<iostream>
using namespace std;

struct Cordinate{
	int x;
	int y;
};
int main(){
	int A = 1000;
	int B = 2000;
	int8_t bytes[8];
	auto f = [&](int var,int x){
		for(int i=3;i>=0;i--){
			int8_t current = 0;
			for(int j=7;j>=0;j--){
				bool isBitSet = false;
				if(x & (1 << (i*8 +j)))
					isBitSet = true;
	//			std::cout <<isBitSet;
				if(isBitSet)
					current |= (1 << j);	
			}
			bytes[var*4 + i] = current;
			//std::cout << std::endl;
		}
	};
	f(0,A);
	//std::cout << std::endl;
	f(1,B);
	//for(int i=7;i>=0;i--)
	//	std::cout << (int) bytes[i] << std::endl;
/*

	int8_t i= byte
	8 bytes
	4 bytes X
	4 bytes Y
*/
	void *ptr = bytes;
	Cordinate *Ptrcordinate = (Cordinate *)(ptr);
	std::cout << (*Ptrcordinate).x << std::endl;
	std::cout << (*Ptrcordinate).y << std::endl;
}
