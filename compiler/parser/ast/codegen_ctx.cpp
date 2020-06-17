#include <libgen.h>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "lang/seq.h"
#include "parser/ast/codegen.h"
#include "parser/ast/codegen_ctx.h"
#include "parser/ast/context.h"
#include "parser/ast/format.h"
#include "parser/ast/transform.h"
#include "parser/ast/transform_ctx.h"
#include "parser/common.h"
#include "parser/ocaml.h"

using fmt::format;
using std::make_pair;
using std::make_shared;
using std::pair;
using std::shared_ptr;
using std::string;
using std::vector;

namespace seq {
namespace ast {

string chop(const string &s) {
  return s.size() && s[0] == '#' ? s.substr(1) : s;
}

LLVMContext::LLVMContext(const string &filename,
                         shared_ptr<RealizationContext> realizations,
                         shared_ptr<ImportContext> imports, seq::Block *block,
                         seq::BaseFunc *base, seq::SeqJIT *jit)
    : Context<LLVMItem::Item>(filename, realizations, imports),
      tryCatch(nullptr), jit(jit) {
  stack.push_front(vector<string>());
  topBaseIndex = topBlockIndex = 0;
  if (block)
    addBlock(block, base);
}

LLVMContext::~LLVMContext() {}

shared_ptr<LLVMItem::Item> LLVMContext::find(const string &name, bool onlyLocal,
                                             bool checkStdlib) const {
  auto i = Context<LLVMItem::Item>::find(name);
  if (i && CAST(i, LLVMItem::Var) && onlyLocal)
    return (getBase() == i->getBase()) ? i : nullptr;
  if (i)
    return i;

  auto stdlib = imports->getImport("");
  if (stdlib && checkStdlib)
    return stdlib->lctx->find(name, onlyLocal, false);

  // auto it = getRealizations()->realizationLookup.find(name);
  // if (it != getRealizations()->realizationLookup.end()) {
  //   auto fit = getRealizations()->funcRealizations.find(it->second);
  //   if (fit != getRealizations()->funcRealizations.end()) {
  //     return make_shared<LLVMItem::Func>(fit->second[name].handle, getBase(),
  //                                        true);
  //   }
  //   // auto cit = getRealizations()->classRealizations.find(it->second);
  //   // if (cit != getRealizations()->classRealizations.end())
  //   //   return make_shared<LLVMItem::Class>(cit->second[name].handle,
  //   //                                       cit->second[name].get);
  // }
  return nullptr;
}

void LLVMContext::addVar(const string &name, seq::Var *v, bool global) {
  add(name, make_shared<LLVMItem::Var>(v, getBase(), global || isToplevel()));
}

void LLVMContext::addType(const string &name, seq::types::Type *t,
                          bool global) {
  add(name, make_shared<LLVMItem::Class>(t, getBase(), global || isToplevel()));
}

void LLVMContext::addFunc(const string &name, seq::BaseFunc *f, bool global) {
  add(name, make_shared<LLVMItem::Func>(f, getBase(), global || isToplevel()));
}

void LLVMContext::addImport(const string &name, const string &import,
                            bool global) {
  add(name,
      make_shared<LLVMItem::Import>(import, getBase(), global || isToplevel()));
}

void LLVMContext::addBlock(seq::Block *newBlock, seq::BaseFunc *newBase) {
  Context<LLVMItem::Item>::addBlock();
  if (newBlock)
    topBlockIndex = blocks.size();
  blocks.push_back(newBlock);
  if (newBase)
    topBaseIndex = bases.size();
  bases.push_back(newBase);
}

void LLVMContext::popBlock() {
  bases.pop_back();
  topBaseIndex = bases.size() - 1;
  while (topBaseIndex && !bases[topBaseIndex])
    topBaseIndex--;
  blocks.pop_back();
  topBlockIndex = blocks.size() - 1;
  while (topBlockIndex && !blocks[topBlockIndex])
    topBlockIndex--;
  Context<LLVMItem::Item>::popBlock();
}

void LLVMContext::initJIT() {
  jit = new seq::SeqJIT();
  auto fn = new seq::Func();
  fn->setName("$jit_0");

  addBlock(fn->getBlock(), fn);
  assert(topBaseIndex == topBlockIndex && topBlockIndex == 0);

  execJIT();
}

void LLVMContext::execJIT(string varName, seq::Expr *varExpr) {
  // static int counter = 0;

  // assert(jit != nullptr);
  // assert(bases.size() == 1);
  // jit->addFunc((seq::Func *)bases[0]);

  // vector<pair<string, shared_ptr<LLVMItem::Item>>> items;
  // for (auto &name : stack.front()) {
  //   auto i = find(name);
  //   if (i && i->isGlobal())
  //     items.push_back(make_pair(name, i));
  // }
  // popBlock();
  // for (auto &i : items)
  //   add(i.first, i.second);
  // if (varExpr) {
  //   auto var = jit->addVar(varExpr);
  //   add(varName, var);
  // }

  // // Set up new block
  // auto fn = new seq::Func();
  // fn->setName(format("$jit_{}", ++counter));
  // addBlock(fn->getBlock(), fn);
  // assert(topBaseIndex == topBlockIndex && topBlockIndex == 0);
}

// void LLVMContext::dump(int pad) {
//   auto ordered = decltype(map)(map.begin(), map.end());
//   for (auto &i : ordered) {
//     std::string s;
//     auto t = i.second.top();
//     if (auto im = t->getImport()) {
//       DBG("{}{:.<25} {}", string(pad*2, ' '), i.first, '<import>');
//       getImports()->getImport(im->getFile())->tctx->dump(pad+1);
//     }
//     else
//       DBG("{}{:.<25} {}", string(pad*2, ' '), i.first,
//       t->getType()->toString(true));
//   }
// }

seq::types::Type *LLVMContext::realizeType(types::ClassTypePtr t) {
  // DBG("[codegen] generating ty {} / {}", t->name, t->toString(true));
  assert(t && t->canRealize());
  auto it = getRealizations()->classRealizations.find(t->name);
  assert(it != getRealizations()->classRealizations.end());
  auto it2 = it->second.find(t->realizeString());
  assert(it2 != it->second.end());
  auto &real = it2->second;
  if (real.handle)
    return real.handle;

  DBG("[codegen] generating ty {}", real.fullName);

  vector<seq::types::Type *> types;
  vector<int> statics;
  for (auto &m : t->explicits)
    if (auto s = m.type->getStatic())
      statics.push_back(s->value);
    else
      types.push_back(realizeType(m.type->getClass()));
  // TODO: function ?!
  if (t->name == "#str") {
    real.handle = seq::types::Str;
  } else if (t->name == "Int" || t->name == "UInt") {
    assert(statics.size() == 1 && types.size() == 0);
    assert(statics[0] >= 1 && statics[0] <= 2048);
    real.handle = seq::types::IntNType::get(statics[0], t->name == "Int");
  } else if (t->name == "#array") {
    assert(types.size() == 1 && statics.size() == 0);
    real.handle = seq::types::ArrayType::get(types[0]);
  } else if (t->name == "ptr") {
    assert(types.size() == 1 && statics.size() == 0);
    real.handle = seq::types::PtrType::get(types[0]);
  } else if (t->name == "generator") {
    assert(types.size() == 1 && statics.size() == 0);
    real.handle = seq::types::GenType::get(types[0]);
  } else if (t->name == "optional") {
    assert(types.size() == 1 && statics.size() == 0);
    real.handle = seq::types::OptionalType::get(types[0]);
  } else {
    vector<string> names;
    vector<seq::types::Type *> types;
    for (auto &m : real.args) {
      names.push_back(m.first);
      types.push_back(realizeType(m.second));
    }
    if (t->isRecord()) {
      vector<string> x;
      for (auto &t : types)
        x.push_back(t->getName());
      auto name = t->name;
      if (name.substr(0, 9) == "#__tuple_")
        name = "";
      real.handle = seq::types::RecordType::get(types, names, chop(name));
    } else {
      auto cls = seq::types::RefType::get(chop(t->name));
      cls->setContents(seq::types::RecordType::get(types, names, ""));
      cls->setDone();
      real.handle = cls;
    }
  }
  return real.handle;
}

shared_ptr<LLVMContext> LLVMContext::getContext(const string &file,
                                                shared_ptr<TypeContext> typeCtx,
                                                seq::SeqModule *module) {
  auto realizations = typeCtx->getRealizations();
  auto imports = typeCtx->getImports();
  auto stdlib = const_cast<ImportContext::Import *>(imports->getImport(""));

  auto block = module->getBlock();
  seq::BaseFunc *base = module;
  stdlib->lctx = make_shared<LLVMContext>(stdlib->filename, realizations,
                                          imports, block, base, nullptr);

  // Now add all realization stubs
  for (auto &ff : realizations->classRealizations)
    for (auto &f : ff.second) {
      auto &real = f.second;
      stdlib->lctx->realizeType(real.type);
      stdlib->lctx->addType(real.fullName, real.handle);
    }
  for (auto &ff : realizations->funcRealizations)
    for (auto &f : ff.second) {
      // Realization: f.second
      auto &real = f.second;
      auto ast = real.ast;
      if (in(ast->attributes, "internal")) {
        DBG("[codegen] generating internal fn {} ~ {}", real.fullName,
            ast->name);
        vector<seq::types::Type *> types;

        // static: has self as arg
        assert(real.type->realizationInfo->baseClass &&
               real.type->realizationInfo->baseClass->getClass());
        seq::types::Type *typ = stdlib->lctx->realizeType(
            real.type->realizationInfo->baseClass->getClass());
        int startI = 1;
        if (ast->args.size() && ast->args[0].name == "self")
          startI = 2;
        // DBG("   realizing {} ~ {}", real.fullName, typ->getName());
        for (int i = startI; i < real.type->args.size(); i++)
          types.push_back(
              stdlib->lctx->realizeType(real.type->args[i]->getClass()));
        real.handle = typ->findMagic(ast->name, types);
      } else {
        DBG("[codegen] generating fn stub {}", real.fullName);
        real.handle = new seq::Func();
      }
      stdlib->lctx->addFunc(real.fullName, real.handle);
    }

  CodegenVisitor c(stdlib->lctx);
  // for (auto &p : pod)
  // c.visitMethods(p);
  c.transform(stdlib->statements.get());

  return make_shared<LLVMContext>(file, realizations, imports, block, base,
                                  nullptr);
}

} // namespace ast
} // namespace seq