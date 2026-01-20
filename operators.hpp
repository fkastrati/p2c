#pragma once
#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <source_location>
#include <string>
#include <vector>
#include <iostream>

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

struct IUSet;

// consumer callback function
typedef std::function<void(void)> ConsumerFn;

// format generic list of strings with delimiter (helper)
std::string join(const std::vector<std::string>& strs, const std::string& delim) {
   std::string result = "";
   bool first = true;
   for (const auto& str : strs) {
      if (first)
         first = false;
      else
         result += delim;
      result += str;
   }
   return result;
}

// an Information Unit (IU) represents an attribute of a query plan
struct IU {
   std::string name;
   Type type;
   std::string varname;

   IU(const std::string& name, Type type) : name(name), type(type), varname(genVar(name)) {}

   // generate unique variable name
   static std::string genVar(const std::string& name) {
      static unsigned varCounter = 1;
      return std::format("{}{}", name, varCounter++);
   }
};

// provide an IU by generating local variable (helper)
void provideIU(IU* iu, const std::string& value) {
   std::print("{} {} = {};\n", tname(iu->type), iu->varname, value);
}

// generate curly-brace block of C++ code (helper)
template<class Fn>
void genBlock(const std::string& str, Fn fn, const std::source_location& location = std::source_location::current()) {
   std::cout << str << "{ //" << location.line() << "; " << location.function_name() << std::endl;
   fn();
   std::cout << "}" << std::endl;
}

// an unordered set of IUs
struct IUSet {
   // set is represented as array; invariant: IUs are sorted by pointer value
   std::vector<IU*> v;

   // empty set constructor
   IUSet() {}

   // move constructor
   IUSet(IUSet&& x) { v = std::move(x.v); }

   // copy constructor
   IUSet(const IUSet& x) { v = x.v; }

   // convert vector to set of IUs (assumes vector is unique, but not sorted)
   explicit IUSet(const std::vector<IU*>& vv) {
      v = vv;
      sort(v.begin(), v.end());
      // check that there are no duplicates
      assert(adjacent_find(v.begin(), v.end()) == v.end());
   }

   // iterate over IUs
   IU** begin() { return v.data(); }
   IU** end() { return v.data() + v.size(); }
   IU* const* begin() const { return v.data(); }
   IU* const* end() const { return v.data() + v.size(); }

   void add(IU* iu) {
      auto it = lower_bound(v.begin(), v.end(), iu);
      if (it == v.end() || *it != iu)
         v.insert(it, iu);  // O(n), not nice
   }

   bool contains(IU* iu) const {
      auto it = lower_bound(v.begin(), v.end(), iu);
      return (it != v.end() && *it == iu);
   }

   unsigned size() const { return v.size(); };
};

// set union operator
IUSet operator|(const IUSet& a, const IUSet& b) {
   IUSet result;
   std::set_union(a.v.begin(), a.v.end(), b.v.begin(), b.v.end(), back_inserter(result.v));
   return result;
}

// set intersection operator
IUSet operator&(const IUSet& a, const IUSet& b) {
   IUSet result;
   std::set_intersection(a.v.begin(), a.v.end(), b.v.begin(), b.v.end(), back_inserter(result.v));
   return result;
}

// set difference operator
IUSet operator-(const IUSet& a, const IUSet& b) {
   IUSet result;
   std::set_difference(a.v.begin(), a.v.end(), b.v.begin(), b.v.end(), back_inserter(result.v));
   return result;
}

// set equality operator
bool operator==(const IUSet& a, const IUSet& b) {
   return equal(a.v.begin(), a.v.end(), b.v.begin(), b.v.end());
}

// format comma-separated list of IU types (helper)
std::string formatTypes(const std::vector<IU*>& ius) {
   std::vector<std::string> iuNames;
   for (IU* iu : ius)
      iuNames.push_back(tname(iu->type));
   return join(iuNames, ",");
}

// format comma-separated list of IU varnames (helper)
std::string formatVarnames(const std::vector<IU*>& ius) {
   std::vector<std::string> varNames;
   for (IU* iu : ius)
      varNames.push_back(iu->varname);
   return join(varNames, ",");
}

//------------------------------------------------------------------------------
// abstract base class of all expressions
//------------------------------------------------------------------------------
struct Exp {
   // compile expression to string
   virtual std::string compile() = 0;
   // set of all IUs used in this expression
   virtual IUSet iusUsed() = 0;
   // destructor
   virtual ~Exp() {};
};

// expression that simply references an IU
struct IUExp : public Exp {
   IU* iu;

   // constructor
   IUExp(IU* iu) : iu(iu) {}
   // destructor
   ~IUExp() {}

   std::string compile() override { return iu->varname; }
   IUSet iusUsed() override { return IUSet({iu}); }
};

// expression that represent a constant value
template<typename T>
requires is_p2c_type<T>
struct ConstExp : public Exp {
   T x;

   // constructor
   ConstExp(T x) : x(x) {};
   // destructor
   ~ConstExp() = default;

   std::string compile() override {
      if constexpr (type_tag<T>::tag == Type::String) {
         return std::format("\"{}\"", x);  // Add quotes for strings
      } else {
         return std::format("{}", x);
      }
   }
   IUSet iusUsed() override { return {}; }
};

// expression that represents function all
struct FnExp : public Exp {
   // function name
   std::string fnName;
   // arguments
   std::vector<std::unique_ptr<Exp>> args;

   // constructor
   FnExp(std::string fnName, std::vector<std::unique_ptr<Exp>>&& v) : fnName(fnName), args(std::move(v)) {}
   // destructor
   ~FnExp() {}

   std::string compile() override {
      std::vector<std::string> strs;
      for (auto& e : args)
         strs.emplace_back(e->compile());
      return std::format("{}({})", fnName, join(strs, ","));
   }

   IUSet iusUsed() override {
      IUSet result;
      for (auto& exp : args)
         for (IU* iu : exp->iusUsed())
            result.add(iu);
      return result;
   }
};

//------------------------------------------------------------------------------
// abstract base class of all operators
//------------------------------------------------------------------------------

struct Operator {
   virtual ~Operator() = default;
   // compute *all* IUs this operator can produce
   virtual IUSet availableIUs() = 0;
   // generate code for operator providing 'required' IUs and pushing them to 'consume' callback
   virtual void produce(const IUSet& required, ConsumerFn consume) = 0;
   // accept visitor
   virtual void accept(OperatorVisitor& v) = 0;
};

// table scan operator
struct Scan : public Operator {
   // IU storage for all available attributes
   std::vector<IU> attributes;
   // relation name
   std::string relName;

   // constructor
   Scan(const std::string& relName) : relName(relName) {
      // get relation info from schema
      auto it = TPCH::schema.find(relName);
      assert(it != TPCH::schema.end());
      auto& rel = it->second;
      // create IUs for all available attributes
      attributes.reserve(rel.size());
      for (auto& att : rel)
         attributes.emplace_back(IU{att.first, att.second});
   }

   // destructor
   ~Scan() {}

   IUSet availableIUs() override {
      IUSet result;
      for (auto& iu : attributes)
         result.add(&iu);
      return result;
   }

   void produce(const IUSet& required, ConsumerFn consume) override {
      genBlock(std::format("for (uint64_t i = 0; i != db.{}.tupleCount; i++)", relName), [&]() {
         for (IU* iu : required)
            provideIU(iu, std::format("db.{}.{}[i]", relName, iu->name));
         consume();
      });
   }

   IU* getIU(const std::string& attName) {
      for (IU& iu : attributes)
         if (iu.name == attName)
            return &iu;
      throw;
   }

   void accept(OperatorVisitor& v) override { v.visit(*this); }
};

// selection operator
struct Selection : public Operator {
   std::unique_ptr<Operator> input;
   std::unique_ptr<Exp> pred;

   // constructor
   Selection(std::unique_ptr<Operator> input, std::unique_ptr<Exp> predicate) : input(std::move(input)), pred(std::move(predicate)) {}
   // destructor
   ~Selection() {}

   IUSet availableIUs() override { return input->availableIUs(); }

   void produce(const IUSet& required, ConsumerFn consume) override {
      input->produce(required | pred->iusUsed(), [&]() {
         genBlock(std::format("if ({})", pred->compile()), [&]() {
            consume();
         });
      });
   }

   void accept(OperatorVisitor& v) override { v.visit(*this); }
};

// map operator (compute new value)
struct Map : public Operator {
   std::unique_ptr<Operator> input;
   std::unique_ptr<Exp> exp;
   IU iu;

   // constructor
   Map(std::unique_ptr<Operator> input, std::unique_ptr<Exp> exp, const std::string& name, Type type)
       : input(std::move(input)), exp(std::move(exp)), iu{name, type} {}

   // destructor
   ~Map() {}

   IUSet availableIUs() override { return input->availableIUs() | IUSet({&iu}); }

   void produce(const IUSet& required, ConsumerFn consume) override {
      input->produce((required | exp->iusUsed()) - IUSet({&iu}), [&]() {
         genBlock("", [&]() {
            provideIU(&iu, exp->compile());
            consume();
         });
      });
   }

   IU* getIU(const std::string& attName) {
      if (iu.name == attName)
         return &iu;
      throw;
   }

   void accept(OperatorVisitor& v) override { v.visit(*this); }
};

// sort operator
struct Sort : public Operator {
   std::unique_ptr<Operator> input;
   std::vector<IU*> keyIUs;
   std::vector<bool> ascending;
   IU v{"vector", Type::Undefined};
   IU cmp{"custom_cmp", Type::Undefined};

   // constructor
   Sort(std::unique_ptr<Operator> input, const std::vector<IU*>& keyIUs, const std::vector<bool> ascending) : input(std::move(input)), keyIUs(keyIUs), ascending(ascending) {}

   // destructor
   ~Sort() {}

   IUSet availableIUs() override { return input->availableIUs(); }

   void produce(const IUSet& required, ConsumerFn consume) override {
      // compute IUs
      IUSet restIUs = required - IUSet(keyIUs);
      std::vector<IU*> allIUs = keyIUs;
      allIUs.insert(allIUs.end(), restIUs.v.begin(), restIUs.v.end());

      // define custom comparator
      genBlock("struct", [&]() {
         genBlock(std::format("bool operator()(const tuple<{0}>& lhs, const tuple<{0}>& rhs) const",
                              formatTypes(allIUs)),
                  [&]() {
            for (size_t i = 0; i != keyIUs.size(); i++) {
               std::print("if (get<{0}>(lhs) != get<{0}>(rhs)) return get<{0}>(lhs) {1} get<{0}>(rhs);\n", i,
                          ascending[i] ? "<" : ">");
            }
            std::print("return false;\n");
         });
      });
      std::print("{};\n", cmp.varname);

      // collect tuples
      std::print("vector<tuple<{}>> {};\n", formatTypes(allIUs), v.varname);
      input->produce(IUSet(allIUs), [&]() {
         std::print("{}.push_back({{{}}});\n", v.varname, formatVarnames(allIUs));
      });

      // sort
      std::print("sort({0}.begin(), {0}.end(), {1});\n", v.varname, cmp.varname);

      // iterate
      genBlock(std::format("for (auto& t : {})", v.varname), [&]() {
         for (unsigned i = 0; i < allIUs.size(); i++)
            if (required.contains(allIUs[i]))
               provideIU(allIUs[i], std::format("get<{}>(t)", i));
         consume();
      });
   };

   void accept(OperatorVisitor& v) override { v.visit(*this); }
};

// abstract base class for aggregate functions using in group by
struct Aggregate {
   IU* inputIU;  // IU to aggregate (is nullptr when aggFn==Count)
   IU resultIU;

   Aggregate(std::string name, IU* _inputIU) : inputIU(_inputIU), resultIU(name, _inputIU->type) {}
   Aggregate(std::string name, Type type) : inputIU(nullptr), resultIU(std::move(name), type) {}

   virtual ~Aggregate() = default;

   virtual std::string genInitValue() = 0;
   virtual std::string genUpdate(std::string oldValueRef) = 0;

};

struct CountAggregate final : Aggregate {
   CountAggregate(std::string name) : Aggregate(name, Type::Integer) {}
   std::string genInitValue() override { return "1"; }
   std::string genUpdate(std::string oldValueRef) override {
      return format("{} += 1", oldValueRef);
   }
};

struct MinAggregate final : Aggregate {
   MinAggregate(std::string name, IU* _inputIU) : Aggregate(name, _inputIU) {}

   std::string genInitValue() override { return format("{}", inputIU->varname); }
   std::string genUpdate(std::string oldValueRef) override {
      return format("{} = std::min({}, {})", oldValueRef, oldValueRef, inputIU->varname);
   }
};

struct SumAggregate final : Aggregate {
   SumAggregate(std::string name, IU* _inputIU) : Aggregate(name, _inputIU) {}

   std::string genInitValue() override { return format("{}", inputIU->varname); }
   std::string genUpdate(std::string oldValueRef) override {
      return format("{} += {}", oldValueRef, inputIU->varname);
   }
};

// group by operator
struct GroupBy : public Operator {
   std::unique_ptr<Operator> input;
   IUSet groupKeyIUs;
   std::vector<std::unique_ptr<Aggregate>> aggs;
   IU ht{"aggHT", Type::Undefined};

   // constructor
   GroupBy(std::unique_ptr<Operator> input, const IUSet& groupKeyIUs) : input(std::move(input)), groupKeyIUs(groupKeyIUs) {}

   // destructor
   ~GroupBy() {}

   void addAggregate(std::unique_ptr<Aggregate> agg) { aggs.emplace_back(std::move(agg)); }

   std::vector<IU*> resultIUs() {
      std::vector<IU*> v;
      for (auto& agg : aggs)
         v.push_back(&agg->resultIU);
      return v;
   }

   IUSet inputIUs() {
      IUSet v;
      for (auto& agg : aggs)
         if (agg->inputIU)
            v.add(agg->inputIU);
      return v;
   }

   IUSet availableIUs() override { return groupKeyIUs | IUSet(resultIUs()); }

   void produce(const IUSet& required, ConsumerFn consume) override {
      // build hash table
      std::print("unordered_map<tuple<{}>, tuple<{}>> {};\n", formatTypes(groupKeyIUs.v), formatTypes(resultIUs()), ht.varname);
      input->produce(groupKeyIUs | inputIUs(), [&]() {
         // insert tuple into hash table
         std::print("auto it = {}.find({{{}}});\n", ht.varname, formatVarnames(groupKeyIUs.v));
         genBlock(format("if (it == {}.end())", ht.varname), [&]() {
            std::vector<std::string> initValues;
            for (auto& agg : aggs)
               initValues.push_back(agg->genInitValue());
            // insert new group
            std::print("{}.insert({{{{{}}}, {{{}}}}});\n", ht.varname, formatVarnames(groupKeyIUs.v), join(initValues, ","));
         });
         genBlock("else", [&]() {
            // update group
            unsigned i = 0;
            for (auto& agg : aggs) {
               std::print("{};\n", agg->genUpdate(std::format("get<{}>(it->second)", i++)));
            }
         });
      });

      // iterate over hash table
      genBlock(format("for (auto& it : {})", ht.varname), [&]() {
         for (unsigned i = 0; i < groupKeyIUs.size(); i++) {
            IU* iu = groupKeyIUs.v[i];
            if (required.contains(iu))
               provideIU(iu, std::format("get<{}>(it.first)", i));
         }
         unsigned i = 0;
         for (auto& agg : aggs) {
            provideIU(&agg->resultIU, std::format("get<{}>(it.second)", i));
            i++;
         }
         consume();
      });
   }

   IU* getIU(const std::string& attName) {
      for (auto& agg : aggs)
         if (agg->resultIU.name == attName)
            return &agg->resultIU;
      throw;
   }

   void accept(OperatorVisitor& v) override { v.visit(*this); }
};

// hash join operator
struct HashJoin : public Operator {
   std::unique_ptr<Operator> left;
   std::unique_ptr<Operator> right;
   // join keys from both inputs, example: left=[a, b] right=[c, d] a=c AND b=d
   std::vector<IU*> leftKeyIUs, rightKeyIUs;
   // variable name for hash table
   IU ht{"joinHT", Type::Undefined};

   // constructor
   HashJoin(std::unique_ptr<Operator> left, std::unique_ptr<Operator> right, const std::vector<IU*>& leftKeyIUs, const std::vector<IU*>& rightKeyIUs)
       : left(std::move(left)), right(std::move(right)), leftKeyIUs(leftKeyIUs), rightKeyIUs(rightKeyIUs) {}

   // destructor
   ~HashJoin() {}

   IUSet availableIUs() override { return left->availableIUs() | right->availableIUs(); }

   void produce(const IUSet& required, ConsumerFn consume) override {
      // figure out where required IUs come from
      IUSet leftRequiredIUs = (required & left->availableIUs()) | IUSet(leftKeyIUs);
      IUSet rightRequiredIUs = (required & right->availableIUs()) | IUSet(rightKeyIUs);
      IUSet leftPayloadIUs = leftRequiredIUs - IUSet(leftKeyIUs);  // these we need to store in hash table as payload

      // build hash table
      std::print("unordered_multimap<tuple<{}>, tuple<{}>> {};\n", formatTypes(leftKeyIUs), formatTypes(leftPayloadIUs.v), ht.varname);
      left->produce(leftRequiredIUs, [&]() {
         // insert tuple into hash table
         std::print("{}.insert({{{{{}}}, {{{}}}}});\n", ht.varname, formatVarnames(leftKeyIUs), formatVarnames(leftPayloadIUs.v));
      });

      // probe hash table
      right->produce(rightRequiredIUs, [&]() {
         // iterate over matches
         genBlock(format("for (auto range = {}.equal_range({{{}}}); range.first!=range.second; range.first++)", ht.varname, formatVarnames(rightKeyIUs)), [&]() {
            // unpack payload
            unsigned countP = 0;
            for (IU* iu : leftPayloadIUs)
               provideIU(iu, std::format("get<{}>(range.first->second)", countP++));
            // unpack keys if needed
            for (unsigned i = 0; i < leftKeyIUs.size(); i++) {
               IU* iu = leftKeyIUs[i];
               if (required.contains(iu))
                  provideIU(iu, std::format("get<{}>(range.first->first)", i));
            }
            // consume
            consume();
         });
      });
   }

   void accept(OperatorVisitor& v) override { v.visit(*this); }
};


struct Limit : public Operator {
   std::unique_ptr<Operator> input;
   uint64_t limit;
   std::string limitVar;
   std::string labelVar;

   Limit(std::unique_ptr<Operator> input, uint64_t limit) 
      : input(std::move(input)), limit(limit) {
      static unsigned counter = 0;
      limitVar = std::format("limit_cnt_{}", counter);
      labelVar = std::format("limit_end_{}", counter++);
   }

   IUSet availableIUs() override { return input->availableIUs(); }

   void produce(const IUSet& required, ConsumerFn consume) override {
      std::print("uint64_t {} = 0;\n", limitVar);
      
      input->produce(required, [&]() {
         std::print("if ({}++ >= {}) goto {};\n", limitVar, limit, labelVar);
         consume();
      });

      // Label to jump to when limit is reached
      std::print("{}:;\n", labelVar);
   }

   void accept(OperatorVisitor& v) override { v.visit(*this); }
};

// print operator is the root operator that outputs the final result (i.e., sink operator)
struct Print : public Operator { 
   std::unique_ptr<Operator> input;
   std::vector<IU*> iusToPrint;

   Print(std::unique_ptr<Operator> input, std::vector<IU*> ius) 
      : input(std::move(input)), iusToPrint(ius) {}

   IUSet availableIUs() override { return input->availableIUs(); }

   void produce(const IUSet& required, ConsumerFn consume) override {
      IUSet reqFromChild = required | IUSet(iusToPrint);

      input->produce(reqFromChild, [&]() {
         for (IU* iu : iusToPrint) { // projection list
            std::print("std::cout << {} << \" \";", iu->varname);
         }
         std::print("std::cout << std::endl;\n");
         
         // Call consume in case there is something above us (though Print is usually root)
         consume();
      });
   }

   void accept(OperatorVisitor& v) override { v.visit(*this); }
};


// -----------------------------------------------------------------------------
// PlanPrinter Visitor: Visualizes the Query Tree
// -----------------------------------------------------------------------------


struct PlanPrinter : public OperatorVisitor {
   int level = 0;

   void indent() {
      for (int i = 0; i < level; ++i) std::cout << "  ";
   }

   void visit(Scan& op) override {
      indent();
      // CORRECTED: Uses 'relName' as defined in operator.hpp
      std::cout << "- Scan (" << op.relName << ")" << std::endl; 
   }

   void visit(Selection& op) override {
      indent();
      // 'pred' is the member name in operator.hpp
      std::cout << "- Selection (filter: " << op.pred->compile() << ")" << std::endl;
      
      level++;
      op.input->accept(*this); // 'input' is the member name
      level--;
   }

   void visit(Map& op) override {
      indent();
      // 'iu.name' comes from the IU struct in operator.hpp
      std::cout << "- Map (compute: " << op.iu.name << ")" << std::endl;
      
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
      indent(); std::cout << "Left:" << std::endl;
      op.left->accept(*this);
      
      indent(); std::cout << "Right:" << std::endl;
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