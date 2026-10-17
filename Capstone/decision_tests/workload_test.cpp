#include "workload.hpp"
#include <cstdio>

int main() {
    auto ops = ob::make_workload(100000);
    int a=0, c=0, b=0, d=0;
    for (auto& op : ops) {
        if (op.type == ob::OpType::Add)    ++a;
        if (op.type == ob::OpType::Cancel) ++c;
        if (op.type == ob::OpType::Bbo)    ++b;
        if (op.type == ob::OpType::Depth)  ++d;
    }
    std::printf("add=%d cancel=%d bbo=%d depth=%d total=%d\n",
                a, c, b, d, a+c+b+d);

    auto ops2 = ob::make_workload(100000);
    bool same = (ops.size() == ops2.size());
    for (size_t i = 0; same && i < ops.size(); ++i) {
        if (ops[i].type != ops2[i].type ||
            ops[i].id   != ops2[i].id   ||
            ops[i].price!= ops2[i].price||
            ops[i].qty  != ops2[i].qty) {
            same = false;
        }
    }
    std::printf("deterministic: %s\n", same ? "yes" : "no");
}