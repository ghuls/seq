#ifndef SEQ_LAMBDA_H
#define SEQ_LAMBDA_H

#include <vector>
#include <initializer_list>

#include "llvm.h"
#include "stage.h"

namespace seq {

	class LambdaStage;

	struct LambdaNode {
		std::vector<LambdaNode *> children;
		LambdaNode(std::initializer_list<LambdaNode *> children);
		virtual llvm::Value *codegen(llvm::BasicBlock *block, bool isFloat) const=0;
	};

	struct IdentNode : LambdaNode {
		llvm::Value *v;
		IdentNode();
		llvm::Value *codegen(llvm::BasicBlock *block, bool isFloat) const override;
	};

	struct LambdaContext {
		LambdaNode *root;
		IdentNode *arg;
		llvm::Function *lambda;
		LambdaContext();
		llvm::Function *codegen(llvm::Module *module, bool isFloat);
	};

	struct LambdaContextProxy {
		operator LambdaContext&();
	};

	LambdaContext& operator+(LambdaContext& lambda, LambdaNode& node);
	LambdaContext& operator-(LambdaContext& lambda, LambdaNode& node);
	LambdaContext& operator*(LambdaContext& lambda, LambdaNode& node);
	LambdaContext& operator/(LambdaContext& lambda, LambdaNode& node);
	LambdaContext& operator+(LambdaNode& node, LambdaContext& lambda);
	LambdaContext& operator-(LambdaNode& node, LambdaContext& lambda);
	LambdaContext& operator*(LambdaNode& node, LambdaContext& lambda);
	LambdaContext& operator/(LambdaNode& node, LambdaContext& lambda);
	LambdaContext& operator+(LambdaContext& lambda, seq_int_t n);
	LambdaContext& operator-(LambdaContext& lambda, seq_int_t n);
	LambdaContext& operator*(LambdaContext& lambda, seq_int_t n);
	LambdaContext& operator/(LambdaContext& lambda, seq_int_t n);
	LambdaContext& operator+(seq_int_t n, LambdaContext& lambda);
	LambdaContext& operator-(seq_int_t n, LambdaContext& lambda);
	LambdaContext& operator*(seq_int_t n, LambdaContext& lambda);
	LambdaContext& operator/(seq_int_t n, LambdaContext& lambda);
	LambdaContext& operator+(LambdaContext& lambda1, LambdaContext& lambda2);
	LambdaContext& operator-(LambdaContext& lambda1, LambdaContext& lambda2);
	LambdaContext& operator*(LambdaContext& lambda1, LambdaContext& lambda2);
	LambdaContext& operator/(LambdaContext& lambda1, LambdaContext& lambda2);

	typedef LambdaContextProxy Lambda;

	class LambdaStage : public Stage {
	private:
		bool isFloat;
		LambdaContext& lambda;
	public:
		explicit LambdaStage(LambdaContext& lambda);
		void validate() override;
		void codegen(llvm::Module *module) override;
		static LambdaStage& make(LambdaContext& lambda);
	};

}

#endif /* SEQ_LAMBDA_H */