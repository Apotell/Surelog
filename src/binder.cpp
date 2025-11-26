// -*- c++ -*-

/*

 Copyright 2019 Alain Dargelas

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

/*
 * File:   binder.cpp
 * Author: hs
 *
 * Created on September 05, 2025, 12:00 PM
 */

#include <uhdm/uhdm.h>
#include <uhdm/vpi_user.h>
#include <uhdm/vpi_visitor.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "Surelog/API/Surelog.h"
#include "Surelog/Common/Session.h"
#include "Surelog/DesignCompile/ObjectBinder.h"

#if defined(_MSC_VER)
#include <Windows.h>
#undef interface
#endif

int main(int argc, const char **argv) {
#if defined(_MSC_VER) && defined(_DEBUG)
  // Redirect cout to file
  std::streambuf *cout_rdbuf = nullptr;
  std::streambuf *cerr_rdbuf = nullptr;
  std::ofstream file;
  if (IsDebuggerPresent() != 0) {
    file.open("cout.txt");
    cout_rdbuf = std::cout.rdbuf(file.rdbuf());
    cerr_rdbuf = std::cerr.rdbuf(file.rdbuf());
  }
#endif

  std::string filepath = argv[1];

  uhdm::Serializer serializer;
  std::vector<vpiHandle> restoredDesigns = serializer.restore(filepath);

  if (restoredDesigns.empty()) {
    std::cerr << filepath << ": empty design." << std::endl;
    return 1;
  }

  SURELOG::Session session;
  SURELOG::ObjectBinder::ForwardComponentMap forwardComponentMap;
  SURELOG::ObjectBinder *const binder = new SURELOG::ObjectBinder(
      &session, forwardComponentMap, serializer, true);

  vpi_show_ids(true);
  uhdm::visit_designs(restoredDesigns, std::cout);
  std::cout << std::string(100, ' ') << std::endl;

  // if (uhdm::Factory *const factory = serializer.getFactory<uhdm::Design>()) {
  //   for (uhdm::Any *object : factory->getObjects()) {
  //     binder->bind(static_cast<uhdm::Design *>(object), false);
  //   }
  // }

  if (uhdm::Factory *const factory = serializer.getFactory<uhdm::RefObj>()) {
    for (uhdm::Any *object : factory->getObjects()) {
      if (object->getUhdmId() == 110411) {
        binder->bindAny(object);
      }
    }
  }

  uhdm::visit_designs(restoredDesigns, std::cout);

#if defined(_MSC_VER) && defined(_DEBUG)
  // Redirect cout back to screen
  if (cout_rdbuf != nullptr) {
    std::cout.rdbuf(cout_rdbuf);
  }
  if (cerr_rdbuf != nullptr) {
    std::cerr.rdbuf(cerr_rdbuf);
  }
  if (file.is_open()) {
    file.flush();
    file.close();
  }
#endif

  return 0;
}
