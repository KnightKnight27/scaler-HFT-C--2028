#include <cstdint>

volatile std::uint64_t counter = 0;

__attribute__((noinline))
	void send() {
		    counter = counter + 1;
	}

int main() {
	    constexpr int N = 1'000'000;

	        for (int i = 0; i < N; ++i) {
			        send();
				    }

		    return 0;
}
