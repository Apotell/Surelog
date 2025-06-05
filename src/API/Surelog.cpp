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

#include "Surelog/API/Surelog.h"

#include <vector>

#include "Surelog/CommandLine/CommandLineParser.h"
#include "Surelog/Common/FileSystem.h"
#include "Surelog/Common/Session.h"
#include "Surelog/Design/Design.h"
#include "Surelog/Design/FileContent.h"
#include "Surelog/DesignCompile/CompileDesign.h"
#include "Surelog/SourceCompile/CompileSourceFile.h"
#include "Surelog/SourceCompile/Compiler.h"
#include "Surelog/SourceCompile/ParseFile.h"
#include "Surelog/SourceCompile/AstListener.h"

namespace SURELOG {

scompiler* start_compiler(Session* session) {
  Compiler* the_compiler = new Compiler(session);
  bool status = the_compiler->compile();
  if (!status) return nullptr;
  return (scompiler*)the_compiler;
}

Design* get_design(scompiler* the_compiler) {
  if (the_compiler) return ((Compiler*)the_compiler)->getDesign();
  return nullptr;
}

void shutdown_compiler(scompiler* the_compiler) {
  if (the_compiler == nullptr) return;
  Compiler* compiler = (Compiler*)the_compiler;
  if (CompileDesign* comp = compiler->getCompileDesign()) {
    comp->getSerializer().purge();
  }
  delete (Compiler*)the_compiler;
}

uhdm::Design* get_uhdm_design(scompiler* compiler) {
  if (Design* design = get_design(compiler)) {
    return design->getUhdmDesign();
  }
  return nullptr;
}

vpiHandle get_vpi_design(scompiler* compiler) {
  vpiHandle design_handle = nullptr;
  Compiler* the_compiler = (Compiler*)compiler;
  if (the_compiler) {
    design_handle = the_compiler->getVpiDesign();
  }
  return design_handle;
}

void walk(scompiler* compiler, AstListener* listener) {
  if (!compiler || !listener) return;
  Compiler* the_compiler = (Compiler*)compiler;
  for (const CompileSourceFile* csf : the_compiler->getCompileSourceFiles()) {
    ParseFile* const parser = csf->getParser();
    FileContent* const fC = parser->getFileContent();
    if (listener->shouldWalkSourceFile(fC->getSession(), fC->getFileId())) {
      const std::vector<VObject>& objects = fC->getVObjects();
      listener->listen(fC->getSession(), fC->getFileId(),
                       parser->getSourceText(), objects.data(), objects.size());
    }
  }
}

bool CompareTrees(scompiler* LHScompiler, scompiler* RHScompiler) {
  Compiler* formattingCompiler = (Compiler*)LHScompiler;
  Compiler* verificationComplier = (Compiler*)RHScompiler;

  const std::vector<CompileSourceFile*>& formattedFiles =
      formattingCompiler->getCompileSourceFiles();
  const std::vector<CompileSourceFile*>& verificationFiles =
      verificationComplier->getCompileSourceFiles();

  FileSystem* formattingFileSystem =
      formattingCompiler->getSession()->getFileSystem();
  FileSystem* verificationFileSystem =
      verificationComplier->getSession()->getFileSystem();
  // Iterate over all formatted files
  for (const auto* formattedFile : formattedFiles) {
    ParseFile* formatParser = formattedFile->getParser();
    FileContent* formatFC = formatParser->getFileContent();
    NodeId formatTree = formatFC->getRootNode();
    const std::vector<VObject>& formatTreeObjects = formatFC->getVObjects();

    const std::string targetFileName =
        ((std::filesystem::path)formattingFileSystem->toPath(
             formattedFile->getFileId()))
            .filename()
            .string();

    // Find matching file in verification set
    const CompileSourceFile* matchedVerificationFile = nullptr;
    for (const auto* verificationFile : verificationFiles) {
      std::string verificationFileName =
          ((std::filesystem::path)verificationFileSystem->toPath(
               verificationFile->getFileId()))
              .filename()
              .string();

      if (verificationFileName == targetFileName) {
        matchedVerificationFile = verificationFile;
        break;
      }
    }

    if (matchedVerificationFile) {
      ParseFile* verificationParser = matchedVerificationFile->getParser();
      FileContent* verificationFC = verificationParser->getFileContent();
      NodeId verificationTree = verificationFC->getRootNode();
      const std::vector<VObject>& verificationTreeObjects =
          verificationFC->getVObjects();

      if (!areIdentical(formatTree, formatTreeObjects, verificationTree,
                        verificationTreeObjects)) {
        return false;  // Trees don't match
      }
    } else {
      std::cout << "No matching file found for " << targetFileName
                << " in verification set.\n";
      return false;  // No match found
    }
  }

  return true;  // All matched trees are identical
}

bool isSkippableType(VObjectType type) {
  return type == VObjectType::paWhite_space || type == VObjectType::ppCR;
}

NodeId getNext(NodeId nodeId, const std::vector<VObject>& objects) {
  while (nodeId && isSkippableType(objects[nodeId].m_type)) {
    nodeId = (objects[nodeId].m_child) ? objects[nodeId].m_child
                                       : objects[nodeId].m_sibling;
  }
  return nodeId;
}

bool areIdentical(NodeId nodeIdA, const std::vector<VObject>& objectsA,
                  NodeId nodeIdB, const std::vector<VObject>& objectsB) {
  nodeIdA = getNext(nodeIdA, objectsA);
  nodeIdB = getNext(nodeIdB, objectsB);

  // Both nodes are null
  if (!nodeIdA && !nodeIdB) return true;

  // One null but the other isn't
  if (!nodeIdA || !nodeIdB) return false;

  // Type mismatch
  if (objectsA[nodeIdA].m_type != objectsB[nodeIdB].m_type) return false;

  // Compare children
  NodeId childA = getNext(objectsA[nodeIdA].m_child, objectsA);
  NodeId childB = getNext(objectsB[nodeIdB].m_child, objectsB);

  while (childA || childB) {
    // Recursively compare corresponding children
    if (!areIdentical(childA, objectsA, childB, objectsB)) return false;

    // Advance to next sibling (skipping whitespace/CR)
    NodeId sibA(0);
    if (childA) {
      sibA = objectsA[childA].m_sibling;
    }
    NodeId sibB(0);
    if (childB) {
      sibB = objectsB[childB].m_sibling;
    }

    childA = getNext(sibA, objectsA);
    childB = getNext(sibB, objectsB);
    ;
  }

  return true;
}

}  // namespace SURELOG
