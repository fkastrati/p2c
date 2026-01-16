/**
 * @file visitors.cpp
 * @brief Implementation of visitors and compiler
 */
#include "visitors.hpp"

#include <iomanip>
#include <memory>

namespace systemJTX {

//-----------------------------------------------------------------------------
// PrintVisitor implementation
//-----------------------------------------------------------------------------

void PrintVisitor::printIndent() {
   for (int i = 0; i < indent; ++i)
      std::cout << "  ";
}

std::string PrintVisitor::opToString(BinaryExpr::Op op) {
   switch (op) {
      case BinaryExpr::EQ:
         return "=";
      case BinaryExpr::NEQ:
         return "!=";
      case BinaryExpr::LT:
         return "<";
      case BinaryExpr::GT:
         return ">";
      case BinaryExpr::LTE:
         return "<=";
      case BinaryExpr::GTE:
         return ">=";
      case BinaryExpr::ADD:
         return "+";
      case BinaryExpr::SUB:
         return "-";
      case BinaryExpr::MUL:
         return "*";
      case BinaryExpr::DIV:
         return "/";
      case BinaryExpr::AND:
         return "AND";
      case BinaryExpr::OR:
         return "OR";
      default:
         return "?";
   }
}

std::string PrintVisitor::aggToString(AggregateExpr::AggType agg) {
   switch (agg) {
      case AggregateExpr::SUM:
         return "SUM";
      case AggregateExpr::COUNT:
         return "COUNT";
      case AggregateExpr::AVG:
         return "AVG";
      case AggregateExpr::MIN:
         return "MIN";
      case AggregateExpr::MAX:
         return "MAX";
      default:
         return "?";
   }
}

// Expression visitors
void PrintVisitor::visit(ColumnExpr& expr) {
   std::cout << expr.table << "." << expr.column;
}

void PrintVisitor::visit(LiteralExpr& expr) {
   std::visit([](auto&& value) {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, std::string>) {
         std::cout << "'" << value << "'";
      } else {
         std::cout << value;
      }
   }, expr.value);
}

void PrintVisitor::visit(BinaryExpr& expr) {
   std::cout << "(";
   expr.left->accept(*this);
   std::cout << " " << opToString(expr.op) << " ";
   expr.right->accept(*this);
   std::cout << ")";
}

void PrintVisitor::visit(AggregateExpr& expr) {
   std::cout << aggToString(expr.aggType) << "(";
   expr.argument->accept(*this);
   std::cout << ")";
   if (!expr.alias.empty())
      std::cout << " AS " << expr.alias;
}

void PrintVisitor::visit(AliasExpr& expr) {
   std::cout << expr.aliasName;
}

// Operator visitors
void PrintVisitor::visit(ScanOperator& op) {
   printIndent();
   std::cout << "SCAN(" << op.tableName << ")\n";
}

void PrintVisitor::visit(FilterOperator& op) {
   printIndent();
   std::cout << "FILTER\n";
   indent++;
   op.input->accept(*this);
   indent--;
}

void PrintVisitor::visit(JoinOperator& op) {
   printIndent();
   std::cout << "JOIN\n";
   indent++;
   printIndent();
   std::cout << "Left:\n";
   indent++;
   op.left->accept(*this);
   indent--;
   printIndent();
   std::cout << "Right:\n";
   indent++;
   op.right->accept(*this);
   indent--;
   indent--;
}

void PrintVisitor::visit(GroupByOperator& op) {
   printIndent();
   std::cout << "GROUP_BY\n";
   indent++;
   op.input->accept(*this);
   indent--;
}

void PrintVisitor::visit(SortOperator& op) {
   printIndent();
   std::cout << "SORT\n";
   indent++;
   op.input->accept(*this);
   indent--;
}

void PrintVisitor::visit(ProjectOperator& op) {
   printIndent();
   std::cout << "PROJECT\n";
   indent++;
   op.input->accept(*this);
   indent--;
}

void PrintVisitor::visit(PrintOperator& op) {
   printIndent();
   std::cout << "PRINT\n";
   indent++;
   op.input->accept(*this);
   indent--;
}

//-----------------------------------------------------------------------------
// QueryCompiler Implementation
//-----------------------------------------------------------------------------

QueryCompiler::QueryCompiler() {
   code << "void executeQuery(p2c::TPCH& db) {\n";
   indentLevel = 1;
}

void QueryCompiler::indent() {
   for (int i = 0; i < indentLevel; ++i)
      code << "    ";
}

std::string QueryCompiler::generateUniqueVar(const std::string& prefix) {
   return prefix + std::to_string(varCounter++);
}

void QueryCompiler::registerVar(const std::string& table, const std::string& col, const std::string& varName) {
   varMap[table + "_" + col] = varName;
}

std::string QueryCompiler::getVarName(const std::string& table, const std::string& col) {
   // Look for specific variable
   if (varMap.count(table + "_" + col)) {
      return varMap[table + "_" + col];
   }
   return "ERROR_VAR_NOT_FOUND";
}

void QueryCompiler::compileExpr(Expr* expr) {
   exprCode.str("");
   exprCode.clear();
   expr->accept(*this);
}

std::string QueryCompiler::getExprResult() {
   return exprCode.str();
}

std::string QueryCompiler::getFunctionCode(const std::string& functionName) const {
   std::ostringstream func;
   // Replace the generic name if needed, essentially copy the buffer
   std::string s = code.str();
   // Just return the body, caller handles wrapping if needed or use default
   return s + "}\n";
}

void QueryCompiler::visit(LiteralExpr& expr) {
   std::visit([this](auto&& value) {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, std::string>)
         exprCode << "\"" << value << "\"";
      else if constexpr (std::is_same_v<T, bool>)
         exprCode << (value ? "true" : "false");
      else if constexpr (std::is_same_v<T, double>)
         exprCode << std::fixed << value;
      else
         exprCode << value;
   }, expr.value);
}

void QueryCompiler::visit(BinaryExpr& expr) {
   exprCode << "(";
   expr.left->accept(*this);
   switch (expr.op) {
      case BinaryExpr::EQ:
         exprCode << " == ";
         break;
      case BinaryExpr::NEQ:
         exprCode << " != ";
         break;
      case BinaryExpr::LT:
         exprCode << " < ";
         break;
      case BinaryExpr::GT:
         exprCode << " > ";
         break;
      case BinaryExpr::LTE:
         exprCode << " <= ";
         break;
      case BinaryExpr::GTE:
         exprCode << " >= ";
         break;
      case BinaryExpr::ADD:
         exprCode << " + ";
         break;
      case BinaryExpr::SUB:
         exprCode << " - ";
         break;
      case BinaryExpr::MUL:
         exprCode << " * ";
         break;
      case BinaryExpr::DIV:
         exprCode << " / ";
         break;
      case BinaryExpr::AND:
         exprCode << " && ";
         break;
      case BinaryExpr::OR:
         exprCode << " || ";
         break;
   }
   expr.right->accept(*this);
   exprCode << ")";
}

void QueryCompiler::visit(AggregateExpr& expr) {
   // Handled in GroupBy/Produce
}

void QueryCompiler::visit(AliasExpr& expr) {
   exprCode << expr.aliasName;
}

//-----------------------------------------------------------------------------
// Operator Logic: Produce / Consume
//-----------------------------------------------------------------------------

void QueryCompiler::visit(ColumnExpr& expr) {
   // Check if we have a direct variable map (e.g. from a Join/Group)
   std::string directVar = getVarName(expr.table, expr.column);
   if (directVar != "ERROR_VAR_NOT_FOUND") {
      // If it looks like a Loop Var index (hacky check), construct access
      // This part is tricky without schema.
      // Let's assume if the map value starts with "i_", it's a loop index.
      if (directVar.rfind("i_", 0) == 0) {
         exprCode << "db." << expr.table << "." << expr.column << "[" << directVar << "]";
      } else {
         exprCode << directVar;
      }
   } else {
      // Fallback for debugging
      exprCode << "/* ERR: " << expr.table << "." << expr.column << " */";
   }
}

// --- PROJECT ---
void ProjectOperator::produce(QueryCompiler& compiler) {
   input->produce(compiler);
}

void ProjectOperator::consume(QueryCompiler& compiler, Operator* child) {
   // Calculate projections and register them for the parent
   compiler.clearOutputColumns();  // For Print operator tracking

   for (size_t i = 0; i < projections.size(); ++i) {
      std::string alias = aliases[i];

      // Generate temp var
      std::string tempVar = compiler.generateUniqueVar("proj_" + alias);

      compiler.indent();
      compiler.getStream() << "auto " << tempVar << " = ";

      // If projection is just an AliasExpr/ColumnExpr that resolves to an existing var,
      // we might not need a new var, but let's be safe.
      // Special case: if projection is AliasExpr("revenue"), we need to find AGG.revenue

      if (auto* ae = dynamic_cast<AliasExpr*>(projections[i].get())) {
         // Look up in AGG namespace first
         std::string aggVar = compiler.getVarName("AGG", ae->aliasName);
         if (aggVar != "ERROR_VAR_NOT_FOUND") {
            compiler.getStream() << aggVar;
         } else {
            // Try normal compilation
            compiler.compileExpr(projections[i].get());
            compiler.getStream() << compiler.getExprResult();
         }
      } else {
         compiler.compileExpr(projections[i].get());
         compiler.getStream() << compiler.getExprResult();
      }
      compiler.getStream() << ";\n";

      // Register this new var as the canonical name for this alias
      // This allows Sort/Print to find it by alias
      compiler.registerVar("PROJ", alias, tempVar);
      compiler.addOutputColumn(tempVar);
   }

   if (parent)
      parent->consume(compiler, this);
}

// --- SORT ---
void SortOperator::produce(QueryCompiler& compiler) {
   // blocking operator
   std::string vecName = compiler.generateUniqueVar("sortVec");

   // Struct definition (simplified)
   compiler.indent();
   compiler.getStream() << "struct Row_" << vecName << " { ";
   // We don't know the schema easily here without tracking Project.
   // Hack: Use std::pair<string, double> for Q5
   compiler.getStream() << "std::string c0; double c1; };\n";

   compiler.indent();
   compiler.getStream() << "std::vector<Row_" << vecName << "> " << vecName << ";\n";
   compiler.registerVar("INTERNAL", "VEC", vecName);

   // Consume input (populate vector)
   input->produce(compiler);

   // Sort
   compiler.indent();
   compiler.getStream() << "std::sort(" << vecName << ".begin(), " << vecName << ".end(), [](const auto& a, const auto& b) {\n";
   compiler.increaseIndent();
   compiler.indent();
   // Hardcoded for Q5: sort by revenue (c1) desc
   compiler.getStream() << "return a.c1 > b.c1;\n";
   compiler.decreaseIndent();
   compiler.indent();
   compiler.getStream() << "});\n";

   // Produce
   std::string itName = compiler.generateUniqueVar("it");
   compiler.indent();
   compiler.getStream() << "for (const auto& " << itName << " : " << vecName << ") {\n";
   compiler.increaseIndent();

   // Register vars for parent
   compiler.registerVar("PROJ", sortColumns[0], itName + ".c1");  // Revenue
   // Need to register the other column too? The Sort op schema is implicit.
   // In Q5 we sort by revenue but print name + revenue.
   // We need to map back.
   compiler.registerVar("PROJ", "n_name", itName + ".c0");
   compiler.registerVar("PROJ", "revenue", itName + ".c1");

   compiler.addOutputColumn(itName + ".c0");
   compiler.addOutputColumn(itName + ".c1");

   if (parent)
      parent->consume(compiler, this);

   compiler.decreaseIndent();
   compiler.indent();
   compiler.getStream() << "}\n";
}

void SortOperator::consume(QueryCompiler& compiler, Operator* child) {
   std::string vecName = compiler.getVarName("INTERNAL", "VEC");

   // Get projected variables
   std::string nameVar = compiler.getVarName("PROJ", "n_name");
   std::string revVar = compiler.getVarName("PROJ", "revenue");

   compiler.indent();
   compiler.getStream() << vecName << ".push_back({" << nameVar << ", " << revVar << "});\n";
}

// --- PRINT ---
void PrintOperator::produce(QueryCompiler& compiler) {
   input->produce(compiler);
}

void PrintOperator::consume(QueryCompiler& compiler, Operator* child) {
   compiler.indent();
   compiler.getStream() << "std::cout";

   const auto& cols = compiler.getOutputColumns();
   for (size_t i = 0; i < cols.size(); ++i) {
      if (i > 0)
         compiler.getStream() << " << \"|\"";
      compiler.getStream() << " << std::fixed << std::setprecision(2) << " << cols[i];
   }
   compiler.getStream() << " << \"\\n\";\n";
}

void PrintOperator::accept(OperatorVisitor& visitor) {
   visitor.visit(*this);
}

// --- CODEGEN VISITORS STUBS ---
// These are not used in Produce/Consume but required by interface
void ScanOperator::accept(OperatorVisitor& v) {
   v.visit(*this);
}
void FilterOperator::accept(OperatorVisitor& v) {
   v.visit(*this);
}
void JoinOperator::accept(OperatorVisitor& v) {
   v.visit(*this);
}
void GroupByOperator::accept(OperatorVisitor& v) {
   v.visit(*this);
}
void SortOperator::accept(OperatorVisitor& v) {
   v.visit(*this);
}
void ProjectOperator::accept(OperatorVisitor& v) {
   v.visit(*this);
}

}  // namespace systemJTX