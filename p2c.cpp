// Viktor Leis, 2023

#include <iostream>
#include <map>
#include <sstream>
#include <string_view>
#include <unordered_map>

#if __has_include(<format>)
#include <format>
#include <print>
#elif __has_include(<fmt/core.h>)
#include <fmt/core.h>
using namespace fmt;
#else
#error "Neither <format> nor libfmt is available. Please install libfmt and link it in your Makefile."
#endif

#include "operator.hpp"

using namespace systemJTX;

// create a function call expression (helper)
template<typename T>
requires is_p2c_type<T>
std::unique_ptr<Exp> makeCallExp(const std::string& fn, IU* iu, const T& x) {
   std::vector<std::unique_ptr<Exp>> v;
   v.push_back(std::make_unique<IUExp>(iu));
   v.push_back(std::make_unique<ConstExp<T>>(x));
   return std::make_unique<FnExp>(fn, std::move(v));
}

template<typename... T>
std::unique_ptr<Exp> makeCallExp(const std::string& fn, std::unique_ptr<T>... args) {
   std::vector<std::unique_ptr<Exp>> v;
   (v.push_back(std::move(args)), ...);
   return std::make_unique<FnExp>(fn, std::move(v));
}

// Print
void produceAndPrint(std::unique_ptr<Operator> root, const std::vector<IU*>& ius, unsigned perfRepeat = 2) {
   genBlock(std::format("for (uint64_t {0} = 0; {0} != {1}; {0}++)", IU::genVar("perfRepeat"), perfRepeat - 1), [&]() {
      root->produce(IUSet(ius), [&]() {
         for (IU* iu : ius)
            std::print("cout << {} << \" \";", iu->varname);
         std::print("cout << endl;\n");
      });
   });
}

////////////////////////////////////////////////////////////////////////////////
void simple_test_query() {
   // 1. Scan
   auto scan = std::make_unique<Scan>("part");
   IU* p_partkey = scan->getIU("p_partkey");
   IU* p_name = scan->getIU("p_name");

   // add the `limit' clause so that we do not print the entire table
   auto limit = std::make_unique<Limit>(std::move(scan), 10);

   auto print = std::make_unique<Print>(std::move(limit), std::vector<IU*>{p_partkey, p_name});

   // DEBUG: Print the tree structure
   std::cout << "--- Query Plan ---" << std::endl;
   PlanPrinter printer;
   print->accept(printer);
   std::cout << "------------------" << std::endl;

   unsigned perfRepeat = 2; // adjust as needed
   genBlock(std::format("for (uint64_t {0} = 0; {0} != {1}; {0}++)", IU::genVar("perfRepeat"), perfRepeat - 1), [&]() {
       // Run the pipeline
       // The consumer lambda is empty because Print handles the output
       print->produce(IUSet{}, [](){}); 
   });

   // produceAndPrint(std::move(limit), {p_partkey, p_name});
}

void tpch_q5() {
   // ------------------------------------------------------------
   // TPC-H Query 5; should return the following on sf1 according to umbra:
   // INDONESIA 55502041.1697
   // VIETNAM 55295086.9967
   // CHINA 53724494.2566
   // INDIA 52035512.0002
   // JAPAN 45410175.6954
   // ------------------------------------------------------------
   // select
   //       n_name,
   //       sum(l_extendedprice * (1 - l_discount)) as revenue
   // from
   //       customer,
   //       orders,
   //       lineitem,
   //       supplier,
   //       nation,
   //       region
   // where
   //       c_custkey = o_custkey
   //       and l_orderkey = o_orderkey
   //       and l_suppkey = s_suppkey
   //       and c_nationkey = s_nationkey
   //       and s_nationkey = n_nationkey
   //       and n_regionkey = r_regionkey
   //       and r_name = 'ASIA'
   //       and o_orderdate >= date '1994-01-01'
   //       and o_orderdate < date '1994-01-01' + interval '1' year
   // group by
   //       n_name
   // order by
   //       revenue desc
   // ------------------------------------------------------------
   {
      auto r = std::make_unique<Scan>("region");
      IU* r_regionkey = r->getIU("r_regionkey");
      IU* r_name = r->getIU("r_name");
      auto r_sel =
          std::make_unique<Selection>(std::move(r), makeCallExp("std::equal_to()", std::make_unique<IUExp>(r_name),
                                                                std::make_unique<ConstExp<std::string_view>>("ASIA")));

      auto n = std::make_unique<Scan>("nation");
      IU* n_nationkey = n->getIU("n_nationkey");
      IU* n_regionkey = n->getIU("n_regionkey");
      IU* n_name = n->getIU("n_name");
      auto join1 = std::make_unique<HashJoin>(std::move(r_sel), std::move(n), std::vector<IU*>{r_regionkey}, std::vector<IU*>{n_regionkey});

      auto c = std::make_unique<Scan>("customer");
      IU* c_custkey = c->getIU("c_custkey");
      IU* c_nationkey = c->getIU("c_nationkey");
      auto join2 = std::make_unique<HashJoin>(std::move(join1), std::move(c), std::vector<IU*>{n_nationkey}, std::vector<IU*>{c_nationkey});

      auto o = std::make_unique<Scan>("orders");
      auto o_orderkey = o->getIU("o_orderkey");
      auto o_custkey = o->getIU("o_custkey");
      auto o_orderdate = o->getIU("o_orderdate");
      auto lowerBoundExp = makeCallExp("std::greater_equal()", o_orderdate, stringToType<date>("1994-01-01", 10).value);
      auto upperBoundExp = makeCallExp("std::less()", o_orderdate, stringToType<date>("1995-01-01", 10).value);
      auto o_sel = std::make_unique<Selection>(std::move(o), makeCallExp("std::logical_and()", std::move(lowerBoundExp), std::move(upperBoundExp)));
      auto join3 = std::make_unique<HashJoin>(std::move(join2), std::move(o_sel), std::vector<IU*>{c_custkey}, std::vector<IU*>{o_custkey});

      auto l = std::make_unique<Scan>("lineitem");
      auto l_orderkey = l->getIU("l_orderkey");
      auto l_suppkey = l->getIU("l_suppkey");
      auto l_extendedprice = l->getIU("l_extendedprice");
      auto l_discount = l->getIU("l_discount");
      auto join4 = std::make_unique<HashJoin>(std::move(join3), std::move(l), std::vector<IU*>{o_orderkey}, std::vector<IU*>{l_orderkey});

      auto s = std::make_unique<Scan>("supplier");
      auto s_suppkey = s->getIU("s_suppkey");
      auto s_nationkey = s->getIU("s_nationkey");
      auto join5 = std::make_unique<HashJoin>(std::move(s), std::move(join4), std::vector<IU*>{s_suppkey, s_nationkey}, std::vector<IU*>{l_suppkey, n_nationkey});

      auto discountPriceExp = makeCallExp("std::multiplies()", std::make_unique<IUExp>(l_extendedprice), makeCallExp("std::minus()", std::make_unique<ConstExp<double>>(1.0), std::make_unique<IUExp>(l_discount)));
      auto discountPriceMap = std::make_unique<Map>(std::move(join5), std::move(discountPriceExp), "revenue", Type::Double);
      auto discountPrice = discountPriceMap->getIU("revenue");

      auto gb = std::make_unique<GroupBy>(std::move(discountPriceMap), IUSet({n_name}));
      gb->addAggregate(std::make_unique<SumAggregate>("revenue", discountPrice));
      auto revenue = gb->getIU("revenue");

      auto sort = std::make_unique<Sort>(std::move(gb), std::vector<IU*>{revenue}, std::vector<bool>{false});


      // DEBUG: Print the tree structure
      // std::cout << "--- Query Plan ---" << std::endl;
      // PlanPrinter printer;
      // sort->accept(printer);
      // std::cout << "\n------------------" << std::endl;

      produceAndPrint(std::move(sort), {n_name, revenue});

   }
}

int main(int argc, char* argv[]) {
   tpch_q5();
   // simple_test_query();
   return 0;
}
