/*
 * simplify.h --- AST simplification transformation.
 *
 * (c) Seq project. All rights reserved.
 * This file is subject to the terms and conditions defined in
 * file 'LICENSE', which is part of this source code package.
 */

#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "parser/ast.h"
#include "parser/common.h"
#include "parser/ocaml/ocaml.h"
#include "parser/visitors/simplify/simplify.h"
#include "parser/visitors/simplify/simplify_ctx.h"

using fmt::format;
using std::deque;
using std::dynamic_pointer_cast;
using std::function;
using std::get;
using std::move;
using std::ostream;
using std::pair;
using std::stack;
using std::static_pointer_cast;

namespace seq {
namespace ast {

using namespace types;

StmtPtr SimplifyVisitor::apply(shared_ptr<Cache> cache, const StmtPtr &node,
                               const string &file,
                               unordered_map<string, pair<string, seq_int_t>> &defines,
                               bool barebones) {
  vector<StmtPtr> stmts;
  auto preamble = make_shared<Preamble>();

  if (!cache->module)
    cache->module = new seq::ir::IRModule("");

  // Load standard library if it has not been loaded.
  if (!in(cache->imports, STDLIB_IMPORT)) {
    // Load the internal module
    auto stdlib = make_shared<SimplifyContext>(STDLIB_IMPORT, cache);
    auto stdlibPath = getImportFile(cache->argv0, STDLIB_INTERNAL_MODULE, "", true);
    if (stdlibPath.empty() ||
        stdlibPath.substr(stdlibPath.size() - 12) != "__init__.seq")
      ast::error("cannot load standard library");
    if (barebones)
      stdlibPath = stdlibPath.substr(0, stdlibPath.size() - 5) + "test__.seq";
    stdlib->setFilename(stdlibPath);
    cache->imports[STDLIB_IMPORT] = {stdlibPath, stdlib};

    // Add __internal class that will store functions needed by other internal classes.
    // We will call them as __internal.fn because directly calling fn will result in a
    // unresolved dependency cycle.
    {
      auto name = "__internal__";
      auto canonical = stdlib->generateCanonicalName(name);
      stdlib->add(SimplifyItem::Type, name, canonical, true);
      // Generate an AST for each POD type. All of them are tuples.
      cache->classes[canonical].ast = make_unique<ClassStmt>(
          canonical, vector<Param>(), vector<Param>(), nullptr, vector<string>{});
      preamble->types.emplace_back(clone(cache->classes[canonical].ast));
    }
    // Add simple POD types to the preamble (these types are defined in LLVM and we
    // cannot properly define them in Seq)
    for (auto &name : {"void", "bool", "byte", "int", "float", "T.None"}) {
      auto canonical = stdlib->generateCanonicalName(name);
      stdlib->add(SimplifyItem::Type, name, canonical, true);
      // Generate an AST for each POD type. All of them are tuples.
      cache->classes[canonical].ast =
          make_unique<ClassStmt>(canonical, vector<Param>(), vector<Param>(), nullptr,
                                 vector<string>{ATTR_INTERNAL, ATTR_TUPLE});
      preamble->types.emplace_back(clone(cache->classes[canonical].ast));
    }
    // Add generic POD types to the preamble
    for (auto &name : vector<string>{"Ptr", "Generator", "Optional", "Int", "UInt"}) {
      auto canonical = stdlib->generateCanonicalName(name);
      stdlib->add(SimplifyItem::Type, name, canonical, true);
      vector<Param> generics;
      auto genName = stdlib->generateCanonicalName("T");
      if (string(name) == "Int" || string(name) == "UInt")
        generics.emplace_back(Param{genName, make_unique<IdExpr>("int"), nullptr});
      else
        generics.emplace_back(Param{genName, nullptr, nullptr});
      auto c =
          make_unique<ClassStmt>(canonical, move(generics), vector<Param>(), nullptr,
                                 vector<string>{ATTR_INTERNAL, ATTR_TUPLE});
      if (name == "Generator")
        c->attributes[ATTR_TRAIT] = "";
      preamble->types.emplace_back(clone(c));
      cache->classes[canonical].ast = move(c);
    }
    // Reserve the following static identifiers.
    for (auto name : {"staticlen", "compile_error", "isinstance", "hasattr"})
      stdlib->generateCanonicalName(name);

    // This code must be placed in a preamble (these are not POD types but are
    // referenced by the various preamble Function.N and Tuple.N stubs)
    stdlib->isStdlibLoading = true;
    stdlib->moduleName = "__internal__";
    auto baseTypeCode = "@internal\n@tuple\nclass pyobj:\n  p: Ptr[byte]\n"
                        "@internal\n@tuple\nclass str:\n  ptr: Ptr[byte]\n  len: int\n";
    SimplifyVisitor(stdlib, preamble).transform(parseCode(stdlibPath, baseTypeCode));
    // Load the standard library
    stdlib->setFilename(stdlibPath);
    stmts.push_back(SimplifyVisitor(stdlib, preamble).transform(parseFile(stdlibPath)));
    // Add __argv__ variable as __argv__: Array[str]
    preamble->globals.push_back(
        SimplifyVisitor(stdlib, preamble)
            .transform(make_unique<AssignStmt>(
                make_unique<IdExpr>("__argv__"), nullptr,
                make_unique<IndexExpr>(make_unique<IdExpr>("Array"),
                                       make_unique<IdExpr>("str")))));
    stdlib->isStdlibLoading = false;
  }

  // The whole standard library has the age of zero to allow back-references.
  cache->age++;
  // Reuse standard library context as it contains all standard library symbols.
  auto ctx = static_pointer_cast<SimplifyContext>(cache->imports[STDLIB_IMPORT].ctx);
  ctx->setFilename(file);
  ctx->moduleName = MODULE_MAIN;
  // Load the command-line defines.
  unordered_map<string, pair<string, seq_int_t>> newDefines;
  for (auto &d : defines) {
    try {
      auto canName = ctx->generateCanonicalName(d.first);
      newDefines[canName] = {d.second.first, stoll(d.second.first)};
      ctx->add(SimplifyItem::Type, d.first, canName, false, true);
    } catch (...) {
      ast::error(format("parameter '{}' is not a valid integer", d.first).c_str());
    }
  }
  defines = newDefines;
  // Prepend __name__ = "__main__".
  stmts.push_back(make_unique<AssignStmt>(make_unique<IdExpr>("__name__"),
                                          make_unique<StringExpr>(MODULE_MAIN)));
  // Transform the input node.
  stmts.emplace_back(SimplifyVisitor(ctx, preamble).transform(node));

  auto suite = make_unique<SuiteStmt>();
  for (auto &s : preamble->types)
    suite->stmts.push_back(move(s));
  for (auto &s : preamble->globals)
    suite->stmts.push_back(move(s));
  for (auto &s : preamble->functions)
    suite->stmts.push_back(move(s));
  for (auto &s : stmts)
    suite->stmts.push_back(move(s));
  return move(suite);
}

StmtPtr SimplifyVisitor::apply(shared_ptr<SimplifyContext> ctx, const StmtPtr &node,
                               const string &file) {
  vector<StmtPtr> stmts;
  auto preamble = make_shared<Preamble>();
  stmts.emplace_back(SimplifyVisitor(move(ctx), preamble).transform(node));
  auto suite = make_unique<SuiteStmt>();
  for (auto &s : preamble->types)
    suite->stmts.push_back(move(s));
  for (auto &s : preamble->globals)
    suite->stmts.push_back(move(s));
  for (auto &s : preamble->functions)
    suite->stmts.push_back(move(s));
  for (auto &s : stmts)
    suite->stmts.push_back(move(s));
  return move(suite);
}

SimplifyVisitor::SimplifyVisitor(shared_ptr<SimplifyContext> ctx,
                                 shared_ptr<Preamble> preamble,
                                 shared_ptr<vector<StmtPtr>> prepend)
    : ctx(move(ctx)), preamble(move(preamble)) {
  prependStmts = prepend ? move(prepend) : make_shared<vector<StmtPtr>>();
}

} // namespace ast
} // namespace seq
