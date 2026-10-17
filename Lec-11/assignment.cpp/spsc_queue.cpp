#include "spsc_queue.hpp"

template class SpscQueue<MutexLock, 1024>;
template class SpscQueue<SpinLock, 1024>;
