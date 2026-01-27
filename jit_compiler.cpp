// Fisnik Kastrati, 2026

#include <dlfcn.h>  // For dlopen, dlsym, dlclose

#include <chrono>
#include <cstdlib>  // For system()
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include "operators.hpp"

using namespace systemJTX;

namespace systemJTX {

class QueryLibrary {
   std::string cppPath;
   std::string soPath;
   void* handle = nullptr;
   bool keepFiles;

public:
   QueryLibrary(const std::string& queryFilename, bool keepFiles = true)
       : cppPath(queryFilename), keepFiles(keepFiles) {
      // Derive .so filename from .cpp path
      std::filesystem::path p(queryFilename);
      soPath = p.replace_extension(".so").string();

      // Construct and execute the compilation command
      std::string command = std::format(
          "g++ -std=c++23 -O3 -shared -fPIC {} -o {}",
          cppPath,
          soPath);

      auto start = std::chrono::high_resolution_clock::now();
      int status = std::system(command.c_str());
      auto end = std::chrono::high_resolution_clock::now();

      if (status != 0) {
         throw std::runtime_error(std::format("Compilation of {} failed", cppPath));
      }

      std::chrono::duration<double, std::milli> duration = end - start;
      std::cout << std::format("Compilation finished in {:.2f} ms\n", duration.count());

      // Dynamically Load the Library
      handle = dlopen(soPath.c_str(), RTLD_NOW);
      if (!handle) {
         throw std::runtime_error(std::format("Cannot load library {}: {}", soPath, dlerror()));
      }
   }

   ~QueryLibrary() {
      if (handle) {
         dlclose(handle);
      }
      if (!keepFiles) {
         std::filesystem::remove(cppPath);
         std::filesystem::remove(soPath);
      }
   }

   // Prevent copies to avoid double dlclose()
   QueryLibrary(const QueryLibrary&) = delete;
   QueryLibrary& operator=(const QueryLibrary&) = delete;

   // Locate the function symbol
   template<typename T>
   T getFunction(const std::string& name) {
      T func = reinterpret_cast<T>(dlsym(handle, name.c_str()));
      if (!func) {
         throw std::runtime_error(std::format("Symbol {} not found: {}", name, dlerror()));
      }
      return func;
   }
};

}  // namespace systemJTX

// -----------------------------------------------------------------------------
// Expression Helpers
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
// Codegen Helpers
// -----------------------------------------------------------------------------

void printHeader(std::ofstream& of) {
   std::print(of,
              "#include <iostream>\n"
              "#include <vector>\n"
              "#include <unordered_map>\n"
              "#include <tuple>\n"
              "#include <algorithm>\n"
              "#include \"types.hpp\"\n"
              "#include \"tpch.hpp\"\n\n"
              "extern \"C\" void execute_query(const systemJTX::TPCH& db) {{\n"
              "using namespace std;\n"
              "using namespace systemJTX;\n");
}

void printFooter(std::ofstream& of) {
   std::print(of, "}}\n");
}

typedef void (*query_func_t)(const TPCH&);

void printPlan(Operator* root) {
   PlanPrinter printer;
   root->accept(printer);
}

void dynamically_load_link(const std::string& db_path, const std::string& query_filename) {
   try {
      QueryLibrary queryLib(query_filename);
      auto execute_query = queryLib.getFunction<query_func_t>("execute_query");

      TPCH db(db_path);
      std::cout << "--- Query ---" << std::endl;
      auto start = std::chrono::high_resolution_clock::now();
      execute_query(db);
      auto end = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double, std::milli> duration = end - start;
      std::cout << "--- End Query ---" << std::endl;
      std::cout << std::format("Query ran in {:.2f} ms", duration.count()) << std::endl;

   } catch (const std::exception& e) {
      std::cerr << "Runtime Error: " << e.what() << std::endl;
   }
}

// -----------------------------------------------------------------------------
// Queries
// -----------------------------------------------------------------------------
/* 
   SELECT p_partkey, p_name
   FROM part LIMIT 10;
*/
void simple_test_query(const std::string& filename) {
   std::ofstream outFile(filename);
   if (!outFile)
      throw std::runtime_error(std::format("Could not open file: {}", filename));

   printHeader(outFile);

   auto scan = std::make_unique<Scan>("part");
   IU* p_partkey = scan->getIU("p_partkey");
   IU* p_name = scan->getIU("p_name");

   auto limit = std::make_unique<Limit>(std::move(scan), 10);
   auto print = std::make_unique<Print>(std::move(limit), std::vector<IU*>{p_partkey, p_name});

   CodeWriter w(outFile, 1);
   {
      auto start = std::chrono::high_resolution_clock::now();
      print->produce(w, IUSet{}, []() {});
      auto end = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double, std::micro> duration = end - start;
      std::cout << std::format("Code gen finished in {:.2f} us", duration.count()) << std::endl;
   }

   printFooter(outFile);
}

/*
   --SELECT l_orderkey, l_quantity
   SELECT l_quantity * 2
   FROM lineitem JOIN orders ON l_orderkey = o_orderkey
*/
void test_join_query(const std::string& filename) {
   std::ofstream outFile(filename);
   if (!outFile)
      throw std::runtime_error(std::format("Could not open file: {}", filename));

   printHeader(outFile);

   auto leftScan = std::make_unique<Scan>("orders");
   IU* o_orderkey = leftScan->getIU("o_orderkey");

   auto rightScan = std::make_unique<Scan>("lineitem");
   IU* l_orderkey = rightScan->getIU("l_orderkey");
   IU* l_quantity = rightScan->getIU("l_quantity");

   auto join = std::make_unique<HashJoin>(std::move(leftScan),
                                          std::move(rightScan),
                                          std::vector<IU*>{o_orderkey},
                                          std::vector<IU*>{l_orderkey});

   auto exp = makeCallExp("std::multiplies()", std::make_unique<IUExp>(l_quantity), std::make_unique<ConstExp<int>>(2));
   auto map = std::make_unique<Map>(std::move(join), std::move(exp), "double_quantity", Type::Double);
   auto double_l_quantity = map->getIU("double_quantity");
   auto limit = std::make_unique<Limit>(std::move(map), 10);
   auto print = std::make_unique<Print>(std::move(limit), std::vector<IU*>{double_l_quantity});

   printPlan(print.get());

   CodeWriter w(outFile, 1);
   unsigned perfRepeat = 2;
   {
      auto start = std::chrono::high_resolution_clock::now();
      w.block(std::format("for (uint64_t {0} = 0; {0} != {1}; {0}++)", IU::genVar("perfRepeat"), perfRepeat - 1), [&]() {
         print->produce(w, IUSet{}, []() {});
      });
      auto end = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double, std::micro> duration = end - start;
      std::cout << std::format("Code gen finished in {:.2f} us", duration.count()) << std::endl;
   }

   printFooter(outFile);
}

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
void tpch_q5(const std::string& filename) {
   std::ofstream outFile(filename);
   if (!outFile)
      throw std::runtime_error(std::format("Could not open file: {}", filename));

   printHeader(outFile);

   // Region & Nation Join
   auto r = std::make_unique<Scan>("region");
   IU* r_regionkey = r->getIU("r_regionkey");
   IU* r_name = r->getIU("r_name");
   auto r_sel = std::make_unique<Selection>(std::move(r), makeCallExp("std::equal_to()", std::make_unique<IUExp>(r_name), std::make_unique<ConstExp<std::string_view>>("ASIA")));

   auto n = std::make_unique<Scan>("nation");
   IU* n_nationkey = n->getIU("n_nationkey");
   IU* n_regionkey = n->getIU("n_regionkey");
   IU* n_name = n->getIU("n_name");
   auto join1 = std::make_unique<HashJoin>(std::move(r_sel), std::move(n), std::vector<IU*>{r_regionkey}, std::vector<IU*>{n_regionkey});

   // Customer Join
   auto c = std::make_unique<Scan>("customer");
   IU* c_custkey = c->getIU("c_custkey");
   IU* c_nationkey = c->getIU("c_nationkey");
   auto join2 = std::make_unique<HashJoin>(std::move(join1), std::move(c), std::vector<IU*>{n_nationkey}, std::vector<IU*>{c_nationkey});

   // Orders Join (with date filters)
   auto o = std::make_unique<Scan>("orders");
   auto o_orderkey = o->getIU("o_orderkey");
   auto o_custkey = o->getIU("o_custkey");
   auto o_orderdate = o->getIU("o_orderdate");
   auto lowerBoundExp = makeCallExp("std::greater_equal()", o_orderdate, stringToType<date>("1994-01-01", 10).value);
   auto upperBoundExp = makeCallExp("std::less()", o_orderdate, stringToType<date>("1995-01-01", 10).value);
   auto o_sel = std::make_unique<Selection>(std::move(o), makeCallExp("std::logical_and()", std::move(lowerBoundExp), std::move(upperBoundExp)));
   auto join3 = std::make_unique<HashJoin>(std::move(join2), std::move(o_sel), std::vector<IU*>{c_custkey}, std::vector<IU*>{o_custkey});

   // Lineitem Join
   auto l = std::make_unique<Scan>("lineitem");
   auto l_orderkey = l->getIU("l_orderkey");
   auto l_suppkey = l->getIU("l_suppkey");
   auto l_extendedprice = l->getIU("l_extendedprice");
   auto l_discount = l->getIU("l_discount");
   auto join4 = std::make_unique<HashJoin>(std::move(join3), std::move(l), std::vector<IU*>{o_orderkey}, std::vector<IU*>{l_orderkey});

   // Supplier Join
   auto s = std::make_unique<Scan>("supplier");
   auto s_suppkey = s->getIU("s_suppkey");
   auto s_nationkey = s->getIU("s_nationkey");
   auto join5 = std::make_unique<HashJoin>(std::move(s), std::move(join4), std::vector<IU*>{s_suppkey, s_nationkey}, std::vector<IU*>{l_suppkey, n_nationkey});

   // Projection: revenue = l_extendedprice * (1 - l_discount)
   auto discountPriceExp = makeCallExp("std::multiplies()", std::make_unique<IUExp>(l_extendedprice), makeCallExp("std::minus()", std::make_unique<ConstExp<double>>(1.0), std::make_unique<IUExp>(l_discount)));
   auto discountPriceMap = std::make_unique<Map>(std::move(join5), std::move(discountPriceExp), "revenue", Type::Double);
   auto discountPrice = discountPriceMap->getIU("revenue");

   // Group By & Aggregate
   auto gb = std::make_unique<GroupBy>(std::move(discountPriceMap), IUSet({n_name}));
   gb->addAggregate(std::make_unique<SumAggregate>("revenue", discountPrice));
   auto revenue = gb->getIU("revenue");

   // Sort & Output
   auto sort = std::make_unique<Sort>(std::move(gb), std::vector<IU*>{revenue}, std::vector<bool>{false});
   auto print = std::make_unique<Print>(std::move(sort), std::vector<IU*>{n_name, revenue});

   printPlan(print.get());

   CodeWriter w(outFile, 1);
   unsigned perfRepeat = 2;
   {
      auto start = std::chrono::high_resolution_clock::now();
      w.block(std::format("for (uint64_t {0} = 0; {0} != {1}; {0}++)", IU::genVar("perfRepeat"), perfRepeat - 1), [&]() {
         print->produce(w, IUSet{}, []() {});
      });
      auto end = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double, std::micro> duration = end - start;
      std::cout << std::format("Code gen finished in {:.2f} us", duration.count()) << std::endl;
   }
   printFooter(outFile);
}

int main(int argc, char* argv[]) {
   tpch_q5("q5.cpp");
   dynamically_load_link("data-generator/output/", "q5.cpp");

   // simple_test_query("q1.cpp");
   // dynamically_load_link("data-generator/output/", "q1.cpp");

   // test_join_query("q_join.cpp");
   // dynamically_load_link("data-generator/output/", "q_join.cpp");

   return 0;
}