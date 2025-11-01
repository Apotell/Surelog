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
 * File:   UhdmChecker.cpp
 * Author: alain
 *
 * Created on January 17, 2020, 9:13 PM
 */

#include "Surelog/DesignCompile/UhdmChecker.h"

#include "Surelog/CommandLine/CommandLineParser.h"
#include "Surelog/Common/FileSystem.h"
#include "Surelog/Common/NodeId.h"
#include "Surelog/Common/Session.h"
#include "Surelog/Design/FileContent.h"
#include "Surelog/Design/ModuleInstance.h"
#include "Surelog/Design/VObject.h"
#include "Surelog/DesignCompile/CompileDesign.h"
#include "Surelog/ErrorReporting/Error.h"
#include "Surelog/ErrorReporting/ErrorDefinition.h"
#include "Surelog/ErrorReporting/Location.h"
#include "Surelog/Library/Library.h"
#include "Surelog/Package/Package.h"
#include "Surelog/SourceCompile/Compiler.h"
#include "Surelog/SourceCompile/SymbolTable.h"
#include "Surelog/SourceCompile/VObjectTypes.h"
#include "Surelog/Utils/StringUtils.h"

// UHDM
#include <uhdm/Serializer.h>
#include <uhdm/uhdm.h>
#include <uhdm/vpi_visitor.h>

#include <cstdint>
#include <iomanip>
#include <ostream>
#include <set>
#include <sstream>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace SURELOG {
// TODO: In the code below, some of the column-range based coverage is
// implemented, the data collection part is. The actual coverage is not and is
// commented out as it is a work in progress

bool UhdmChecker::registerFile(const FileContent* fC,
                               std::set<std::string_view>& moduleNames) {
  const NodeId nodeId = fC->getRootNode();
  NodeId startId = fC->Child(nodeId);
  if (!startId) startId = fC->Sibling(nodeId);
  if (!startId) return false;

  std::stack<NodeId> stack;
  stack.emplace(startId);

  const PathId fileId = fC->getFileId();

  FileNodeCoverMap::iterator fileItr = m_fileNodeCoverMap.find(fC);
  if (fileItr == m_fileNodeCoverMap.end()) {
    RangesMap uhdmCover;
    m_fileNodeCoverMap.emplace(fC, uhdmCover);
    fileItr = m_fileNodeCoverMap.find(fC);
  }
  RangesMap& uhdmCover = fileItr->second;
  bool skipModule = false;
  NodeId endModuleNode;
  while (!stack.empty()) {
    const NodeId id = stack.top();
    stack.pop();

    const VObject& current = fC->Object(id);
    bool skip = false;
    VObjectType type = current.m_type;
    if (type == VObjectType::END) skip = true;

    // Skip macro expansion which resides in another file (header)
    PathId fid = fC->getFileId(id);
    if (fid != fileId) {
      if (const NodeId siblingId = fC->Sibling(id)) stack.emplace(siblingId);
      continue;
    }

    if (type == VObjectType::paModule_declaration) {
      NodeId stId = fC->sl_collect(id, VObjectType::STRING_CONST,
                                   VObjectType::paAttr_spec);
      if (stId) {
        std::string name =
            StrCat(fC->getLibrary()->getName(), "@", fC->SymName(stId));
        if (moduleNames.find(name) == moduleNames.end()) {
          skipModule = true;
        }
      }
      endModuleNode = fC->Parent(id);
      endModuleNode = fC->Sibling(endModuleNode);
    }
    if (type == VObjectType::paDescription || type == VObjectType::ENDCASE ||
        type == VObjectType::ENDTASK || type == VObjectType::ENDFUNCTION ||
        type == VObjectType::ENDMODULE || type == VObjectType::ENDINTERFACE ||
        type == VObjectType::ENDPACKAGE || type == VObjectType::ENDCLOCKING ||
        type == VObjectType::ENDCLASS || type == VObjectType::ENDGENERATE ||
        type == VObjectType::ENDCONFIG ||
        type == VObjectType::paEndcelldefine_directive ||
        type == VObjectType::ENDGROUP || type == VObjectType::ENDPRIMITIVE ||
        type == VObjectType::ENDTABLE || type == VObjectType::ENDPROGRAM ||
        type == VObjectType::ENDCHECKER || type == VObjectType::ENDPROPERTY ||
        type == VObjectType::ENDSPECIFY || type == VObjectType::ENDSEQUENCE ||
        type == VObjectType::paPort_declaration ||
        type == VObjectType::paPort_list || type == VObjectType::paPort ||
        type == VObjectType::paConditional_generate_construct ||
        type == VObjectType::paGenerate_module_conditional_statement ||
        type == VObjectType::paGenerate_interface_conditional_statement ||
        type == VObjectType::paLoop_generate_construct ||
        type == VObjectType::paGenerate_module_loop_statement ||
        type == VObjectType::paGenerate_interface_loop_statement ||
        type == VObjectType::paGenerate_region ||
        ((type == VObjectType::paPackage_or_generate_item_declaration) &&
         !fC->Child(id)) ||  // SEMICOLUMN ALONE ;
        type == VObjectType::paGenerate_begin_end_block) {
      RangesMap::iterator lineItr = uhdmCover.find(current.m_startLine);
      if (lineItr != uhdmCover.end()) {
        uhdmCover.erase(lineItr);
      }
      skip = true;  // Only skip the item itself
    }
    if (type == VObjectType::paPort_declaration_list ||
        type == VObjectType::paBit_select || type == VObjectType::paSelect ||
        type == VObjectType::IF || type == VObjectType::ELSE ||
        type == VObjectType::OPEN_PARENS || type == VObjectType::CLOSE_PARENS) {
      skip = true;  // Only skip the item itself
    }
    if (type == VObjectType::paGenvar_declaration) {
      // Skip the item and its' children
      RangesMap::iterator lineItr = uhdmCover.find(current.m_startLine);
      if (lineItr != uhdmCover.end()) {
        uhdmCover.erase(lineItr);
      }
      continue;
    }
    SURELOG::VObjectType parentType = fC->Type(current.m_parent);
    if ((type == VObjectType::STRING_CONST) &&
        ((parentType ==
          VObjectType::paModule_declaration) ||  // endmodule : name
         (parentType ==
          VObjectType::paPackage_declaration) ||  // endpackage : name
         (parentType ==
          VObjectType::paFunction_body_declaration) ||  // endfunction  : name
         (parentType == VObjectType::paTask_declaration) ||   // endtask : name
         (parentType == VObjectType::paClass_declaration) ||  // endclass : name
         (parentType ==
          VObjectType::paName_of_instance) ||  // instance name, slight problem,
                                               // the isntance name will be
                                               // "covered" even in generate
                                               // branches that are not covered.
         (parentType == VObjectType::paModule_nonansi_header) ||  // module name
         (parentType == VObjectType::paType_declaration)))        // struct name
    {
      RangesMap::iterator lineItr = uhdmCover.find(current.m_startLine);
      if (skipModule == false) {
        uint16_t from = fC->Column(id);
        uint16_t to = fC->EndColumn(id);
        if (lineItr != uhdmCover.end()) {
          bool found = false;
          for (ColRange& crange : lineItr->second) {
            if ((crange.from >= from) && (crange.to <= to)) {
              found = true;
              crange.from = from;
              crange.to = to;
              crange.covered = Status::COVERED;
              break;
            }
          }
          if (found == false) {
            ColRange crange;
            crange.from = from;
            crange.to = to;
            crange.covered = Status::COVERED;
            lineItr->second.emplace_back(crange);
          }
        } else {
          Ranges ranges;
          ColRange crange;
          crange.from = from;
          crange.to = to;
          crange.covered = Status::COVERED;
          ranges.emplace_back(crange);
          uhdmCover.emplace(current.m_startLine, ranges);
        }
      }
      skip = true;  // Only skip the item itself
    }

    if (const NodeId siblingId = fC->Sibling(id)) stack.emplace(siblingId);
    if (const NodeId childId = fC->Child(id)) stack.emplace(childId);
    if (!skip && !skipModule) {
      uint16_t from = fC->Column(id);
      uint16_t to = fC->EndColumn(id);
      RangesMap::iterator lineItr = uhdmCover.find(current.m_startLine);
      if (lineItr != uhdmCover.end()) {
        bool found = false;
        for (ColRange& crange : lineItr->second) {
          if ((crange.from >= from) && (crange.to <= to)) {
            found = true;
            crange.from = from;
            crange.to = to;
            crange.covered = Status::EXIST;
            break;
          }
        }
        if (!found) {
          ColRange crange;
          crange.from = from;
          crange.to = to;
          crange.covered = Status::EXIST;
          lineItr->second.emplace_back(crange);
        }
      } else {
        Ranges ranges;
        ColRange crange;
        crange.from = from;
        crange.to = to;
        crange.covered = Status::EXIST;
        ranges.emplace_back(crange);
        uhdmCover.emplace(current.m_startLine, ranges);
      }
    }
    if (id == endModuleNode) {
      skipModule = false;
    }
  }
  return true;
}

bool UhdmChecker::reportHtml(PathId uhdmFileId, float overallCoverage) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();
  ErrorContainer* const errors = m_session->getErrorContainer();

  const PathId reportFileId =
      fileSystem->getCheckerHtmlFile(uhdmFileId, symbols);

  std::ostream& report = fileSystem->openForWrite(reportFileId);
  if (report.bad()) {
    fileSystem->close(report);
    return false;
  }
  report << "\n<!DOCTYPE html>\n<html>\n<head>\n<style>\nbody {\n\n}\np "
            "{\nfont-size: 14px;\n}</style>\n";
  report << "<h2 style=\"text-decoration: underline\">"
         << "Overall Coverage: " << std::setprecision(3) << overallCoverage
         << "%</h2>\n";
  uint32_t fileIndex = 1;
  std::string allUncovered;
  static std::multimap<int32_t, std::string> orderedCoverageMap;
  for (const auto& [fC, uhdmCover] : m_fileNodeCoverMap) {
    std::vector<std::string> fileContentLines;
    if (!fileSystem->readLines(fC->getFileId(), fileContentLines)) {
      fileSystem->close(report);
      return false;
    }
    const std::string filepath(fileSystem->toPath(fC->getFileId()));
    const PathId chkFileId =
        fileSystem->getCheckerHtmlFile(uhdmFileId, fileIndex, symbols);
    const std::string fname(
        std::get<1>(fileSystem->getLeaf(chkFileId, symbols)));
    std::ostream& reportF = fileSystem->openForWrite(chkFileId);
    if (reportF.bad()) {
      fileSystem->close(report);
      fileSystem->close(reportF);
      return false;
    }
    reportF << "\n<!DOCTYPE html>\n<html>\n<head>\n<style>\nbody {\n\n}\np "
               "{\nfont-size: 14px;\n}</style>\n";

    float cov = 0.0f;
    const auto& itr = m_fileCoverageMap.find(fC->getFileId());
    cov = itr->second;
    std::stringstream strst;
    strst << std::setprecision(3) << cov;

    const std::string coverage = StrCat(" Cov: ", strst.str(), "% ");
    const std::string fileStatGreen = StrCat(
        "<div style=\"overflow: hidden;\"> <h3 style=\"background-color: "
        "#82E0AA; margin:0; min-width: 110px; padding:10; float: left; \">",
        coverage,
        "</h3> <h3 style=\"margin:0; padding:10; float: left; \"> <a href=",
        fname + "> ", filepath, "</a></h3></div>\n");
    const std::string fileStatPink = StrCat(
        "<div style=\"overflow: hidden;\"> <h3 style=\"background-color: "
        "#FFB6C1; margin:0; min-width: 110px; padding:10; float: left; \">",
        coverage,
        "</h3> <h3 style=\"margin:0; padding:10; float: left; \"> <a href=",
        fname + "> ", filepath, "</a></h3></div>\n");
    const std::string fileStatRed = StrCat(
        "<div style=\"overflow: hidden;\"> <h3 style=\"background-color: "
        "#FF0000; margin:0; min-width: 110px; padding:10; float: left; \">",
        coverage,
        "</h3> <h3 style=\"margin:0; padding:10; float: left; \"> <a href=",
        fname + "> ", filepath, "</a></h3></div>\n");
    const std::string fileStatWhite =
        StrCat("<h3 style=\"margin:0; padding:0 \"> <a href=" + fname + ">",
               filepath, "</a> ", coverage, "</h3>\n");

    reportF << "<h3>" << filepath << coverage << "</h3>\n";
    bool uncovered = false;
    std::string pinkCoverage;
    std::string redCoverage;
    int32_t line = 0;
    for (const auto& lineText : fileContentLines) {
      ++line;
      RangesMap::const_iterator cItr = uhdmCover.find(line);

      if (cItr == uhdmCover.end()) {
        reportF << "<pre style=\"margin:0; padding:0 \">" << std::setw(4)
                << line << ": " << lineText << "</pre>\n";  // white
      } else {
        const Ranges& ranges = cItr->second;
        bool covered = false;
        bool exist = false;
        bool unsupported = false;
        for (const ColRange& crange : ranges) {
          switch (crange.covered) {
            case EXIST:
              exist = true;
              break;
            case COVERED:
              covered = true;
              break;
            case UNSUPPORTED:
              unsupported = true;
              break;
          }
        }

        if (lineText.empty()) {
          Location loc(fC->getFileId(), line, 1);
          Error err(ErrorDefinition::UHDM_WRONG_COVERAGE_LINE, loc);
          errors->addError(err);
        }
        if (exist && covered && (!unsupported)) {
          // reportF << "<pre style=\"background-color: #FFFFE0; margin:0;
          // padding:0; display: inline-block\">" << std::setw (4) <<
          // std::to_string(line) << ": " << "</pre> <pre
          // style=\"background-color: #C0C0C0; margin:0; padding:0; display:
          // inline-block \">" << lineText << "</pre>\n";  // grey
          reportF << "<pre style=\"background-color: #C0C0C0; margin:0; "
                     "padding:0 \">"
                  << std::setw(4) << std::to_string(line) << ": " << lineText
                  << "</pre>\n";  // grey
        } else if (exist && (!unsupported)) {
          reportF
              << "<pre id=\"id" << line
              << R"(" style="background-color: #FFB6C1; margin:0; padding:0 ">)"
              << std::setw(4) << std::to_string(line) << ": " << lineText
              << "</pre>\n";  // pink
          if (uncovered == false) {
            StrAppend(&allUncovered, "<pre></pre>\n");
            StrAppend(&allUncovered, fileStatWhite);
            StrAppend(&allUncovered, "<pre></pre>\n");
            uncovered = true;
          }
          pinkCoverage = fileStatPink;
          StrAppend(
              &allUncovered,
              "<pre style=\"background-color: #FFB6C1; margin:0; padding:0 \"> "
              "<a href=",
              fname, "#id", line, ">", lineText, "</a></pre>\n");
        } else if (unsupported) {
          reportF
              << "<pre id=\"id" << line
              << R"(" style="background-color: #FF0000; margin:0; padding:0 ">)"
              << std::setw(4) << std::to_string(line) << ": " << lineText
              << "</pre>\n";  // red
          if (uncovered == false) {
            StrAppend(&allUncovered, "<pre></pre>\n");
            StrAppend(&allUncovered, fileStatWhite);
            StrAppend(&allUncovered, "<pre></pre>\n");
            uncovered = true;
          }
          redCoverage = fileStatRed;
          StrAppend(
              &allUncovered,
              "<pre style=\"background-color: #FF0000; margin:0; padding:0 \"> "
              "<a href=",
              fname, "#id", line, ">", lineText, "</a></pre>\n");
        } else {
          reportF << "<pre style=\"background-color: #C0C0C0; margin:0; "
                     "padding:0 \">"
                  << std::setw(4) << std::to_string(line) << ": " << lineText
                  << "</pre>\n";  // grey
        }
      }
    }
    if (!redCoverage.empty()) {
      orderedCoverageMap.emplace(static_cast<int32_t>(cov), redCoverage);
    } else {
      if (!pinkCoverage.empty()) {
        orderedCoverageMap.emplace(static_cast<int32_t>(cov), pinkCoverage);
      }
    }
    if (uncovered == false) {
      orderedCoverageMap.emplace(static_cast<int32_t>(cov), fileStatGreen);
    }
    reportF << "</body>\n</html>\n";
    fileSystem->close(reportF);
    fileIndex++;
  }
  for (const auto& covFile : orderedCoverageMap) {
    report << covFile.second << "\n";
  }

  report << "<h2 style=\"text-decoration: underline\">"
         << "All Uncovered: "
         << "</h2>\n";
  report << allUncovered << "\n";
  report << "</body>\n</html>\n";
  fileSystem->close(report);
  return true;
}

void UhdmChecker::mergeColumnCoverage() {
  for (auto& fileItr : m_fileNodeCoverMap) {
    RangesMap& uhdmCover = fileItr.second;
    for (auto& cItr : uhdmCover) {
      Ranges& ranges = cItr.second;
      Ranges merged;
      for (const ColRange& crange : ranges) {
        if (crange.from >= crange.to) {
        } else {
          merged.emplace_back(crange);
        }
      }
      cItr.second = merged;
    }
  }
}

float UhdmChecker::reportCoverage(PathId uhdmFileId) {
  SymbolTable* const symbols = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();
  const PathId reportFileId = fileSystem->getCheckerFile(uhdmFileId, symbols);

  std::ostream& report = fileSystem->openForWrite(reportFileId);
  if (report.bad()) {
    fileSystem->close(report);
    return false;
  }

  int32_t overallUncovered = 0;
  int32_t overallLineNb = 0;
  for (auto& [fC, uhdmCover] : m_fileNodeCoverMap) {
    bool fileNamePrinted = false;
    int32_t lineNb = 0;
    int32_t uncovered = 0;
    int32_t firstUncoveredLine = 0;
    for (auto& cItr : uhdmCover) {
      Ranges& ranges = cItr.second;
      bool exist = false;
      bool covered = false;
      bool unsupported = false;
      for (ColRange& crange : ranges) {
        switch (crange.covered) {
          case EXIST:
            exist = true;
            break;
          case COVERED:
            covered = true;
            break;
          case UNSUPPORTED:
            unsupported = true;
            break;
        }
      }

      lineNb++;
      overallLineNb++;
      if ((exist && (!covered)) || unsupported) {
        if (fileNamePrinted == false) {
          firstUncoveredLine = cItr.first;
          report << "\n\n"
                 << fileSystem->toPath(fC->getFileId()) << ":" << cItr.first
                 << ": "
                 << " Missing models\n";
          fileNamePrinted = true;
        }
        report << "Line: " << cItr.first << "\n";
        uncovered++;
        overallUncovered++;
      }
    }
    float coverage = 0;
    if (lineNb == 0)
      coverage = 100.0f;
    else
      coverage = (lineNb - uncovered) * 100.0f / lineNb;
    if (uncovered) {
      report << "File coverage: " << std::setprecision(3) << coverage << "%\n";
      m_coverageMap.emplace(
          coverage, std::make_pair(fC->getFileId(), firstUncoveredLine));
    }
    m_fileCoverageMap.emplace(fC->getFileId(), coverage);
  }
  float overallCoverage = 0.0f;
  if (overallLineNb == 0)
    overallCoverage = 100.0f;
  else
    overallCoverage =
        (overallLineNb - overallUncovered) * 100.0f / overallLineNb;
  report << "\nOverall coverage: " << std::setprecision(3) << overallCoverage
         << "%\n";
  report << "\nOrdered coverage:\n";
  for (const auto& covFile : m_coverageMap) {
    report << covFile.second.first << ":" << covFile.second.second << ": "
           << std::setprecision(3) << covFile.first << "% "
           << "\n";
  }
  fileSystem->close(report);
  return overallCoverage;
}

void UhdmChecker::annotate() {
  SymbolTable* const symbols = m_session->getSymbolTable();
  FileSystem* const fileSystem = m_session->getFileSystem();
  uhdm::Serializer& s = m_compileDesign->getSerializer();
  const auto& objects = s.getAllObjects();
  for (const auto& obj : objects) {
    const uhdm::BaseClass* bc = obj.first;
    if (!bc) continue;
    bool unsupported = false;
    uhdm::UhdmType ot = bc->getUhdmType();
    if ((ot == uhdm::UhdmType::UnsupportedExpr) ||
        (ot == uhdm::UhdmType::UnsupportedStmt) ||
        (ot == uhdm::UhdmType::UnsupportedTypespec))
      unsupported = true;
    PathId fnId = fileSystem->toPathId(bc->getFile(), symbols);
    const auto& fItr = m_fileMap.find(fnId);
    if (fItr != m_fileMap.end()) {
      const FileContent* fC = fItr->second;
      FileNodeCoverMap::iterator fileItr = m_fileNodeCoverMap.find(fC);
      if (fileItr != m_fileNodeCoverMap.end()) {
        RangesMap& uhdmCover = fileItr->second;
        RangesMap::iterator cItr = uhdmCover.find(bc->getStartLine());
        // uint16_t from = bc->getStartColumn();
        // uint16_t to = bc->getEndColumn();

        if (cItr != uhdmCover.end()) {
          // bool found = false;

          for (ColRange& crange : cItr->second) {
            //  if ((crange.from >= from) && (crange.to <= to)) {
            //    found = true;
            //    crange.from = from;
            //    crange.to = to;
            if (unsupported)
              crange.covered = Status::UNSUPPORTED;
            else
              crange.covered = Status::COVERED;
            /*    } else if ((crange.from <= from) && (crange.to >= to)) {
                  if (crange.from < from) {
                    ColRange crange1;
                    crange1.from = crange.from;
                    crange1.to = from;
                    crange1.covered = Status::EXIST;
                    cItr->second.emplace_back(crange1);
                  }
                  if (crange.to > to) {
                    ColRange crange1;
                    crange1.from = to;
                    crange1.to = crange.to;
                    crange1.covered = Status::EXIST;
                    cItr->second.emplace_back(crange1);
                  }
                  found = true;
                  crange.from = from;
                  crange.to = to;
                  if (unsupported)
                    crange.covered = Status::UNSUPPORTED;
                  else
                    crange.covered = Status::COVERED;
                } else if ((from < crange.from) && (to > crange.from) && (to <
               crange.to)) { crange.from = to; ColRange crange1; crange1.from =
               from; crange1.to = to; crange1.covered = Status::COVERED;
                  cItr->second.emplace_back(crange1);
                } else if ((from < crange.to) && (from > crange.from) && (to >
               crange.to)) { crange.to = from; ColRange crange1; crange1.from =
               from; crange1.to = to; crange1.covered = Status::COVERED;
                  cItr->second.emplace_back(crange1);
                } */
          }
          /*
                    if (found == false) {
                      ColRange crange;
                      crange.from = from;
                      crange.to = to;
                      if (unsupported)
                        crange.covered = Status::UNSUPPORTED;
                      else
                        crange.covered = Status::COVERED;
                      cItr->second.emplace_back(crange);
                    }
          */
        }
      }
    }
  }
}

void collectUsedFileContents(std::set<const FileContent*>& files,
                             std::set<std::string_view>& moduleNames,
                             ModuleInstance* instance) {
  if (instance) {
    DesignComponent* def = instance->getDefinition();
    if (def) {
      moduleNames.emplace(def->getName());
      for (auto file : def->getFileContents()) {
        if (file) files.insert(file);
      }
    }
    for (uint32_t index = 0; index < instance->getNbChildren(); index++) {
      collectUsedFileContents(files, moduleNames, instance->getChildren(index));
    }
  }
}

bool UhdmChecker::check(PathId uhdmFileId) {
  FileSystem* const fileSystem = m_session->getFileSystem();
  // Register all objects location in file content
  CommandLineParser* const clp = m_session->getCommandLineParser();
  SymbolTable* const symbols = m_session->getSymbolTable();
  std::set<const FileContent*> files;
  std::set<std::string_view> moduleNames;
  for (ModuleInstance* top : m_design->getTopLevelModuleInstances()) {
    collectUsedFileContents(files, moduleNames, top);
  }
  for (const auto& packInfo : m_design->getPackageDefinitions()) {
    Package* pack = packInfo.second;
    for (auto file : pack->getFileContents()) {
      if (file) files.insert(file);
    }
  }

  for (const FileContent* fC : files) {
    if (!clp->createCache()) {
      std::string_view fileName =
          std::get<1>(fileSystem->getLeaf(fC->getFileId(), symbols));
      if ((fileName == "uvm_pkg.sv") || (fileName == "ovm_pkg.sv")) {
        continue;
      }
    }
    m_fileMap.emplace(fC->getFileId(), fC);
    registerFile(fC, moduleNames);
  }

  // Annotate UHDM object coverage
  annotate();

  mergeColumnCoverage();

  if (!fileSystem->mkdirs(
          fileSystem->getCheckerDir(clp->fileUnit(), symbols))) {
    return false;
  }

  // Report uncovered objects
  float overallCoverage = reportCoverage(uhdmFileId);
  reportHtml(uhdmFileId, overallCoverage);
  return true;
}

}  // namespace SURELOG
