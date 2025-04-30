package main

import (
	"fmt"
)

/*
#cgo CXXFLAGS: -I/usr/lib/
#cgo LDFLAGS: -static -L/usr/lib/ -L./libs -lslsfs -lcryptopp -lcassandra_static -lrdkafka++ -lrdkafka -lrdkafka++ -luv -lcurl -lssl -lcrypto -lnghttp2 -lz -lzstd -lbrotlienc -lbrotlidec -lbrotlicommon -lboost_coroutine -lboost_chrono -lboost_thread -lboost_system -lstdc++ -lm -ldl
#include "libslsfs.h"
*/
import "C"

func main() {
	fmt.Println("{\"hello\": \"world\"}")
	//C.start_datafunction()
	return
}

/*
func Main(obj map[string]interface{}) map[string]interface{} {
  // do your work
  name, ok := obj["name"].(string)
  if !ok {
    name = "world"
  }
  msg := make(map[string]interface{})
  msg["message"] = "Hello, " + name + "!"
  // log in stdout or in stderr
  log.Printf("name=%s\n", name)
  // encode the result back in json
  return msg
}
*/
