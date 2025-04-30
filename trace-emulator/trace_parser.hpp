#ifndef TRACEREADER_HPP__
#define TRACEREADER_HPP__

#include <iostream>
#include <fstream>
#include <iterator>
#include <string>
#include <sstream>
#include <vector>

using namespace std;

enum access_type {
    read_op,
    write_op
};

class trace_row {
    public:
        long timestamp;
        long invocation_id;
        double blob_bytes;
        string app_name;
        string blob_name;
        enum access_type access_type;
};

class trace_parser {
    private:
        vector<trace_row> rows;
        trace_row parse_row(string row)
        {
            trace_row to_return;
            string unparsed_row [11] = {};

            stringstream stream;
            stream.str(row);

            for (size_t i = 0; i < 11; i++)
            {
                string col;
                if (getline(stream, col, ',')) {
                    unparsed_row[i] = col;
                }
                else {
                    throw runtime_error("trace_parser: Invalid trace file");
                }
            }

            to_return.timestamp = stol(unparsed_row[0]);
            to_return.app_name = unparsed_row[3];
            to_return.invocation_id = stol(unparsed_row[4]); // Why do I have this?
            to_return.blob_name = unparsed_row[5];
            to_return.blob_bytes = stod(unparsed_row[8]);
            to_return.access_type = (unparsed_row[10] == "True") ? write_op : read_op;


            return to_return;
        }

    public:
        trace_parser (string file_name, size_t num_rows_to_read)
        {
            ifstream trace (file_name);
            string string_row;

            if (trace.is_open()) {
                for (size_t i = 0; i < num_rows_to_read; i++) {
                    if (getline(trace, string_row)) {
                        rows.push_back(parse_row(string_row));
                    } else {
                        throw runtime_error("trace_parser: not enough rows to read");
                    }
                }
            }
            else {
                throw runtime_error("trace_parser: Trace reader could not open trace file");
            }

        };
        vector<trace_row>::iterator trace_begin ()
        {
            return rows.begin();
        }
        vector<trace_row>::iterator trace_end ()
        {
            return rows.end();
        }

    std::size_t trace_size() { return rows.size(); }
};

#endif // TRACEREADER_HPP__
