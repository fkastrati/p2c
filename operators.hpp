// Fisnik Kastrati, 2026

#pragma once
#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <ostream>
#include <print>
#include <source_location>
#include <string>
#include <vector>

#include "tpch.hpp"
#include "types.hpp"

namespace systemJTX {

// -----------------------------------------------------------------------------
// Forward Declarations
// -----------------------------------------------------------------------------
struct Scan;
struct Selection;
struct Map;
struct Sort;
struct GroupBy;
struct HashJoin;
struct Limit;
struct Print;

struct OperatorVisitor {
   virtual ~OperatorVisitor() = default;
   virtual void visit(Scan& op) = 0;
   virtual void visit(Selection& op) = 0;
   virtual void visit(Map& op) = 0;
   virtual void visit(Sort& op) = 0;
   virtual void visit(GroupBy& op) = 0;
   virtual void visit(HashJoin& op) = 0;
   virtual void visit(Limit& op) = 0;
   virtual void visit(Print& op) = 0;
};

// -----------------------------------------------------------------------------
// Information Unit (IU)
// -----------------------------------------------------------------------------
struct IU {
   std::string name;
   Type type;
   std::string varname;

   IU(const std::string& name, Type type) : name(name), type(type), varname(genVar(name)) {}

   static std::string genVar(const std::string& name) {
      static unsigned varCounter = 1;
      return std::format("{}{}", name, varCounter++);
   }
};

// -----------------------------------------------------------------------------
// CodeWriter: State Management for Indentation and Output
// -----------------------------------------------------------------------------
struct CodeWriter {
   std::ostream& out;
   int level;

   CodeWriter(std::ostream& out, int level = 0) : out(out), level(level) {}

   // Write an indented line with a newline
   template<typename... Args>
   void line(std::format_string<Args...> fmt, Args&&... args) {
      indent();
      std::print(out, fmt, std::forward<Args>(args)...);
      std::print(out, "\n");
   }

   // Declare an IU variable
   void declare(IU* iu, const std::string& value) {
      line("{} {} = {};", tname(iu->type), iu->varname, value);
   }

   // Create a scoped block { ... } with automatic indentation handling
   template<typename Fn>
   void block(const std::string& header, Fn fn, const std::source_location& loc = std::source_location::current()) {
      indent();
      if (!header.empty()) {
         std::print(out, "{} ", header);
      }
      std::print(out, "{{ // generated from line {}; {}\n", loc.line(), loc.function_name());

      level++;
      fn();  // Execute generation logic inside the block
      level--;

      indent();
      std::print(out, "}}\n");
   }

   void indent() {
      for (int i = 0; i < level; ++i)
         out << "   ";
   }
};

// -----------------------------------------------------------------------------
// IUSet and Expressions
// -----------------------------------------------------------------------------

inline std::string join(const std::vector<std::string>& strs, const std::string& delim) {
   std::string result = "";
   bool first = true;
   for (const auto& str : strs) {
      if (!first)
         result += delim;
      result += str;
      first = false;
   }
   return result;
}

struct IUSet {
   std::vector<IU*> v;
   IUSet() {}
   IUSet(const std::vector<IU*>& vv) {
      v = vv;
      std::sort(v.begin(), v.end());
      assert(std::adjacent_find(v.begin(), v.end()) == v.end());
   }

   IU** begin() { return v.data(); }
   IU** end() { return v.data() + v.size(); }
   IU* const* begin() const { return v.data(); }
   IU* const* end() const { return v.data() + v.size(); }

   void add(IU* iu) {
      auto it = std::lower_bound(v.begin(), v.end(), iu);
      if (it == v.end() || *it != iu)
         v.insert(it, iu);
   }
   bool contains(IU* iu) const {
      auto it = std::lower_bound(v.begin(), v.end(), iu);
      return (it != v.end() && *it == iu);
   }
   unsigned size() const { return v.size(); };
};

inline IUSet operator|(const IUSet& a, const IUSet& b) {
   IUSet result;
   std::set_union(a.v.begin(), a.v.end(), b.v.begin(), b.v.end(), std::back_inserter(result.v));
   return result;
}
inline IUSet operator&(const IUSet& a, const IUSet& b) {
   IUSet result;
   std::set_intersection(a.v.begin(), a.v.end(), b.v.begin(), b.v.end(), std::back_inserter(result.v));
   return result;
}
inline IUSet operator-(const IUSet& a, const IUSet& b) {
   IUSet result;
   std::set_difference(a.v.begin(), a.v.end(), b.v.begin(), b.v.end(), std::back_inserter(result.v));
   return result;
}

inline std::string formatTypes(const std::vector<IU*>& ius) {
   std::vector<std::string> names;
   for (IU* iu : ius)
      names.push_back(tname(iu->type));
   return join(names, ",");
}
inline std::string formatVarnames(const std::vector<IU*>& ius) {
   std::vector<std::string> names;
   for (IU* iu : ius)
      names.push_back(iu->varname);
   return join(names, ",");
}

struct Exp {
   virtual std::string compile() = 0;
   virtual IUSet iusUsed() = 0;
   virtual ~Exp() {};
};

struct IUExp : public Exp {
   IU* iu;
   IUExp(IU* iu) : iu(iu) {}
   std::string compile() override { return iu->varname; }
   IUSet iusUsed() override { return IUSet({iu}); }
};

template<typename T>
requires is_p2c_type<T>
struct ConstExp : public Exp {
   T x;
   ConstExp(T x) : x(x) {};
   std::string compile() override {
      if constexpr (type_tag<T>::tag == Type::String)
         return std::format("\"{}\"", x);
      return std::format("{}", x);
   }
   IUSet iusUsed() override { return {}; }
};

struct FnExp : public Exp {
   std::string fnName;
   std::vector<std::unique_ptr<Exp>> args;
   FnExp(std::string name, std::vector<std::unique_ptr<Exp>>&& v) : fnName(name), args(std::move(v)) {}
   std::string compile() override {
      std::vector<std::string> strs;
      for (auto& e : args)
         strs.emplace_back(e->compile());
      return std::format("{}({})", fnName, join(strs, ","));
   }
   IUSet iusUsed() override {
      IUSet res;
      for (auto& e : args)
         for (IU* iu : e->iusUsed())
            res.add(iu);
      return res;
   }
};

// -----------------------------------------------------------------------------
// Operators
// -----------------------------------------------------------------------------

typedef std::function<void(void)> ConsumerFn;

struct Operator {
   virtual ~Operator() = default;
   virtual IUSet availableIUs() = 0;
   virtual void produce(CodeWriter& w, const IUSet& required, ConsumerFn consume) = 0;
   virtual void accept(OperatorVisitor& v) = 0;
};

struct Scan : public Operator {
   std::vector<IU> attributes;
   std::string relName;

   Scan(const std::string& relName) : relName(relName) {
      auto it = TPCH::schema.find(relName);
      assert(it != TPCH::schema.end());
      for (auto& att : it->second)
         attributes.emplace_back(IU{att.first, att.second});
   }

   IUSet availableIUs() override {
      IUSet res;
      for (auto& iu : attributes)
         res.add(&iu);
      return res;
   }

   void produce(CodeWriter& w, const IUSet& required, ConsumerFn consume) override {
      w.block(std::format("for (uint64_t i = 0; i != db.{}.tupleCount; i++)", relName), [&] {
         for (IU* iu : required)
            w.declare(iu, std::format("db.{}.{}[i]", relName, iu->name));
         consume();
      });
   }

   IU* getIU(const std::string& name) {
      for (auto& iu : attributes)
         if (iu.name == name)
            return &iu;
      throw std::runtime_error("Scan IU not found: " + name);
   }
   void accept(OperatorVisitor& v) override { v.visit(*this); }
};

struct Selection : public Operator {
   std::unique_ptr<Operator> input;
   std::unique_ptr<Exp> pred;

   Selection(std::unique_ptr<Operator> in, std::unique_ptr<Exp> p) : input(std::move(in)), pred(std::move(p)) {}
   IUSet availableIUs() override { return input->availableIUs(); }

   void produce(CodeWriter& w, const IUSet& required, ConsumerFn consume) override {
      input->produce(w, required | pred->iusUsed(), [&] {
         w.block(std::format("if ({})", pred->compile()), [&] {
            consume();
         });
      });
   }
   void accept(OperatorVisitor& v) override { v.visit(*this); }
};

struct Map : public Operator {
   std::unique_ptr<Operator> input;
   std::unique_ptr<Exp> exp;
   IU iu;

   Map(std::unique_ptr<Operator> in, std::unique_ptr<Exp> e, const std::string& n, Type t)
       : input(std::move(in)), exp(std::move(e)), iu{n, t} {}

   IUSet availableIUs() override { return input->availableIUs() | IUSet({&iu}); }

   void produce(CodeWriter& w, const IUSet& required, ConsumerFn consume) override {
      input->produce(w, (required | exp->iusUsed()) - IUSet({&iu}), [&] {
         w.block("", [&] {
            w.declare(&iu, exp->compile());
            consume();
         });
      });
   }
   IU* getIU(const std::string& name) {
      if (iu.name == name)
         return &iu;
      throw std::runtime_error("Map IU not found");
   }
   void accept(OperatorVisitor& v) override { v.visit(*this); }
};

struct Sort : public Operator {
   std::unique_ptr<Operator> input;
   std::vector<IU*> keyIUs;
   std::vector<bool> ascending;
   IU v{"vector", Type::Undefined}, cmp{"custom_cmp", Type::Undefined};

   Sort(std::unique_ptr<Operator> in, const std::vector<IU*>& keys, const std::vector<bool> asc)
       : input(std::move(in)), keyIUs(keys), ascending(asc) {}

   IUSet availableIUs() override { return input->availableIUs(); }

   void produce(CodeWriter& w, const IUSet& required, ConsumerFn consume) override {
      IUSet restIUs = required - IUSet(keyIUs);
      std::vector<IU*> allIUs = keyIUs;
      allIUs.insert(allIUs.end(), restIUs.v.begin(), restIUs.v.end());

      w.block("struct", [&] {
         w.block(std::format("bool operator()(const tuple<{0}>& lhs, const tuple<{0}>& rhs) const", formatTypes(allIUs)), [&] {
            for (size_t i = 0; i != keyIUs.size(); i++)
               w.line("if (get<{0}>(lhs) != get<{0}>(rhs)) return get<{0}>(lhs) {1} get<{0}>(rhs);", i, ascending[i] ? "<" : ">");
            w.line("return false;");
         });
      });
      w.line("{};", cmp.varname);
      w.line("vector<tuple<{}>> {};", formatTypes(allIUs), v.varname);

      input->produce(w, IUSet(allIUs), [&] {
         w.line("{}.push_back({{{}}});", v.varname, formatVarnames(allIUs));
      });

      w.line("sort({0}.begin(), {0}.end(), {1});", v.varname, cmp.varname);

      w.block(std::format("for (auto& t : {})", v.varname), [&] {
         for (unsigned i = 0; i < allIUs.size(); i++)
            if (required.contains(allIUs[i]))
               w.declare(allIUs[i], std::format("get<{}>(t)", i));
         consume();
      });
   }
   void accept(OperatorVisitor& v) override { v.visit(*this); }
};

struct Aggregate {
   IU* inputIU;
   IU resultIU;
   Aggregate(std::string name, IU* in) : inputIU(in), resultIU(name, in->type) {}
   Aggregate(std::string name, Type type) : inputIU(nullptr), resultIU(name, type) {}
   virtual ~Aggregate() = default;
   virtual std::string genInitValue() = 0;
   virtual std::string genUpdate(std::string oldValueRef) = 0;
};

struct CountAggregate final : Aggregate {
   CountAggregate(std::string n) : Aggregate(n, Type::Integer) {}
   std::string genInitValue() override { return "1"; }
   std::string genUpdate(std::string ref) override { return std::format("{} += 1", ref); }
};

struct SumAggregate final : Aggregate {
   SumAggregate(std::string n, IU* in) : Aggregate(n, in) {}
   std::string genInitValue() override { return std::format("{}", inputIU->varname); }
   std::string genUpdate(std::string ref) override { return std::format("{} += {}", ref, inputIU->varname); }
};

struct GroupBy : public Operator {
   std::unique_ptr<Operator> input;
   IUSet groupKeyIUs;
   std::vector<std::unique_ptr<Aggregate>> aggs;
   IU ht{"aggHT", Type::Undefined};

   GroupBy(std::unique_ptr<Operator> in, const IUSet& keys) : input(std::move(in)), groupKeyIUs(keys) {}

   void addAggregate(std::unique_ptr<Aggregate> agg) { aggs.emplace_back(std::move(agg)); }

   // Lookup aggregate result IUs by name
   IU* getIU(const std::string& attName) {
      for (auto& agg : aggs)
         if (agg->resultIU.name == attName)
            return &agg->resultIU;
      throw std::runtime_error("Aggregate IU not found: " + attName);
   }

   std::vector<IU*> resultIUs() {
      std::vector<IU*> res;
      for (auto& agg : aggs)
         res.push_back(&agg->resultIU);
      return res;
   }

   IUSet inputIUs() {
      IUSet res;
      for (auto& agg : aggs)
         if (agg->inputIU)
            res.add(agg->inputIU);
      return res;
   }

   IUSet availableIUs() override { return groupKeyIUs | IUSet(resultIUs()); }

   void produce(CodeWriter& w, const IUSet& required, ConsumerFn consume) override {
      w.line("unordered_map<tuple<{}>, tuple<{}>> {};", formatTypes(groupKeyIUs.v), formatTypes(resultIUs()), ht.varname);

      input->produce(w, groupKeyIUs | inputIUs(), [&] {
         w.line("auto it = {}.find({{{}}});", ht.varname, formatVarnames(groupKeyIUs.v));
         w.block(std::format("if (it == {}.end())", ht.varname), [&] {
            std::vector<std::string> inits;
            for (auto& agg : aggs)
               inits.push_back(agg->genInitValue());
            w.line("{}.insert({{{{{}}}, {{{}}}}});", ht.varname, formatVarnames(groupKeyIUs.v), join(inits, ","));
         });
         w.block("else", [&] {
            unsigned i = 0;
            for (auto& agg : aggs)
               w.line("{};", agg->genUpdate(std::format("get<{}>(it->second)", i++)));
         });
      });

      w.block(std::format("for (auto& it : {})", ht.varname), [&] {
         for (unsigned i = 0; i < groupKeyIUs.size(); i++)
            if (required.contains(groupKeyIUs.v[i]))
               w.declare(groupKeyIUs.v[i], std::format("get<{}>(it.first)", i));
         unsigned i = 0;
         for (auto& agg : aggs)
            w.declare(&agg->resultIU, std::format("get<{}>(it.second)", i++));
         consume();
      });
   }
   void accept(OperatorVisitor& v) override { v.visit(*this); }
};

struct HashJoin : public Operator {
   std::unique_ptr<Operator> left, right;
   std::vector<IU*> leftKeyIUs, rightKeyIUs;
   IU ht{"joinHT", Type::Undefined};

   HashJoin(std::unique_ptr<Operator> l, std::unique_ptr<Operator> r, const std::vector<IU*>& lk, const std::vector<IU*>& rk)
       : left(std::move(l)), right(std::move(r)), leftKeyIUs(lk), rightKeyIUs(rk) {}

   IUSet availableIUs() override { return left->availableIUs() | right->availableIUs(); }

   void produce(CodeWriter& w, const IUSet& required, ConsumerFn consume) override {
      IUSet leftReq = (required & left->availableIUs()) | IUSet(leftKeyIUs);
      IUSet rightReq = (required & right->availableIUs()) | IUSet(rightKeyIUs);
      IUSet leftPayload = leftReq - IUSet(leftKeyIUs);

      w.line("unordered_multimap<tuple<{}>, tuple<{}>> {};", formatTypes(leftKeyIUs), formatTypes(leftPayload.v), ht.varname);
      left->produce(w, leftReq, [&] {
         w.line("{}.insert({{{{{}}}, {{{}}}}});", ht.varname, formatVarnames(leftKeyIUs), formatVarnames(leftPayload.v));
      });

      right->produce(w, rightReq, [&] {
         auto loop = std::format("for (auto range = {}.equal_range({{{}}}); range.first!=range.second; range.first++)", ht.varname, formatVarnames(rightKeyIUs));
         w.block(loop, [&] {
            unsigned countP = 0;
            for (IU* iu : leftPayload)
               w.declare(iu, std::format("get<{}>(range.first->second)", countP++));
            for (unsigned i = 0; i < leftKeyIUs.size(); i++)
               if (required.contains(leftKeyIUs[i]))
                  w.declare(leftKeyIUs[i], std::format("get<{}>(range.first->first)", i));
            consume();
         });
      });
   }
   void accept(OperatorVisitor& v) override { v.visit(*this); }
};

struct Limit : public Operator {
   std::unique_ptr<Operator> input;
   uint64_t limit;
   std::string limitVar, labelVar;

   Limit(std::unique_ptr<Operator> in, uint64_t l) : input(std::move(in)), limit(l) {
      limitVar = IU::genVar("limit_cnt_");
      labelVar = IU::genVar("limit_end_");
   }

   IUSet availableIUs() override { return input->availableIUs(); }

   void produce(CodeWriter& w, const IUSet& required, ConsumerFn consume) override {
      w.line("uint64_t {} = 0;", limitVar);
      input->produce(w, required, [&] {
         w.line("if ({}++ >= {}) goto {};", limitVar, limit, labelVar);
         consume();
      });
      w.line("{}:;", labelVar);
   }
   void accept(OperatorVisitor& v) override { v.visit(*this); }
};

struct Print : public Operator {
   std::unique_ptr<Operator> input;
   std::vector<IU*> iusToPrint;

   Print(std::unique_ptr<Operator> in, std::vector<IU*> ius) : input(std::move(in)), iusToPrint(ius) {}
   IUSet availableIUs() override { return input->availableIUs(); }

   void produce(CodeWriter& w, const IUSet& required, ConsumerFn consume) override {
      input->produce(w, required | IUSet(iusToPrint), [&] {
         w.indent();
         std::print(w.out, "std::cout ");
         for (IU* iu : iusToPrint)
            std::print(w.out, "<< {} << ' ' ", iu->varname);
         std::print(w.out, "<< '\\n';\n");
         consume();
      });
   }
   void accept(OperatorVisitor& v) override { v.visit(*this); }
};

// -----------------------------------------------------------------------------
// PlanPrinter Visitor
// -----------------------------------------------------------------------------
struct PlanPrinter : public OperatorVisitor {
   int level = 0;
   void indent() {
      for (int i = 0; i < level; ++i)
         std::cout << "  ";
   }
   void visit(Scan& op) override {
      indent();
      std::cout << "- Scan (" << op.relName << ")" << std::endl;
   }
   void visit(Selection& op) override {
      indent();
      std::cout << "- Selection (" << op.pred->compile() << ")" << std::endl;
      level++;
      op.input->accept(*this);
      level--;
   }
   void visit(Map& op) override {
      indent();
      std::cout << "- Map (" << op.iu.name << ")" << std::endl;
      level++;
      op.input->accept(*this);
      level--;
   }
   void visit(Sort& op) override {
      indent();
      std::cout << "- Sort" << std::endl;
      level++;
      op.input->accept(*this);
      level--;
   }
   void visit(GroupBy& op) override {
      indent();
      std::cout << "- GroupBy" << std::endl;
      level++;
      op.input->accept(*this);
      level--;
   }
   void visit(HashJoin& op) override {
      indent();
      std::cout << "- HashJoin" << std::endl;
      level++;
      indent();
      std::cout << "L:" << std::endl;
      op.left->accept(*this);
      indent();
      std::cout << "R:" << std::endl;
      op.right->accept(*this);
      level--;
   }
   void visit(Limit& op) override {
      indent();
      std::cout << "- Limit (" << op.limit << ")" << std::endl;
      level++;
      op.input->accept(*this);
      level--;
   }
   void visit(Print& op) override {
      indent();
      std::cout << "- Print" << std::endl;
      level++;
      op.input->accept(*this);
      level--;
   }
};

}  // namespace systemJTX