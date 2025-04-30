#ifndef EMULATION_NODE_HPP__
#define EMULATION_NODE_HPP__

#include <string>
#include <future>

using namespace std;

class emulation_node
{
    public:
        emulation_node() {};
        virtual int read_request(string filename, double bytes, shared_ptr<promise<string>> result) = 0;
        virtual int write_request(string filename, double bytes, shared_ptr<promise<string>> result) = 0;
};
#endif
