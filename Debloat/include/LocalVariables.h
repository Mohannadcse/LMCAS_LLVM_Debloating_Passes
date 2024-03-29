/*
 * LocalVariables.h
 *
 *  Created on: Aug 22, 2020
 *      Author: ma481
 */

#ifndef SOURCE_DIRECTORY__DEBLOAT_INCLUDE_LOCALVARIABLES_H_
#define SOURCE_DIRECTORY__DEBLOAT_INCLUDE_LOCALVARIABLES_H_

#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <set>

#include "Utility.h"

using namespace llvm;
using namespace std;

namespace lmcas
{
    class LocalVariables
    {
    private:
        typedef tuple<bool, vector<LoadInst *>, uint64_t> postNeckGepInfo;
        ofstream logger;
        vector<Instruction *> instList;

        void handleLocalPrimitiveUsesAfterNeck(Module &, map<string, uint64_t> &,
                                               map<AllocaInst *, uint64_t>,
                                               vector<Instruction *>,
                                               map<AllocaInst *, string>, string);
        void handlePtrToStructAfterNeck(Module &,
                                        map<tuple<string, uint64_t, int>, uint64_t> &,
                                        string);
        void handlePtrToNestedStructAfterNeck(
            map<tuple<string, string, uint64_t, uint64_t, int>, uint64_t> &,
            raw_string_ostream &);
        void
        replacePtrToStructPostNeck(vector<pair<GetElementPtrInst *, postNeckGepInfo>>,
                                   raw_string_ostream &);
        // void inspectInitalizationPreNeck(Module&, map<tuple<string, uint64_t, int>,
        // uint64_t> &, string);
        void inspectInitalizationPreNeck(
            Module &, map<tuple<string, uint64_t, int>, uint64_t> &, string);
        // void handleStructInOtherMethods(Function*, map<tuple<string, uint64_t,
        // int>, uint64_t> &, raw_string_ostream&);
        void handleStructInOtherMethods(Function *,
                                        map<tuple<string, uint64_t, int>, uint64_t> &,
                                        raw_string_ostream &, int);

        bool processGepInstrStruct(llvm::GetElementPtrInst *gep,
                                   tuple<string, uint64_t, int> structInfo);
        bool processGepInstrPtrStruct(llvm::GetElementPtrInst *gep,
                                      tuple<string, uint64_t, int> structInfo);
        bool processGepInstrNestedStruct(
            llvm::GetElementPtrInst *mainGEP, llvm::GetElementPtrInst *elemGEP,
            tuple<string, string, uint64_t, uint64_t, int> structInfo, int flag);

        void constantConversionStrctVars(Module &, GetElementPtrInst *, string,
                                         uint64_t, raw_string_ostream &,
                                         int cntxtFlg);

        /*
         * the following functions handle various cases of nested structs
         */
        void identifyNestedStrctPattern(
            Module &, map<tuple<string, string, uint64_t, uint64_t, int>, uint64_t> &,
            string);

    public:
        void testing(Module &);
        void handleStringVars(Module &, map<uint64_t, pair<uint64_t, string>>, string,
                              Function *);
        void handlePrimitiveLocalVariables(Module &, map<string, uint64_t> &, string,
                                           Function *);
        void handleStructLocalVars(Module &,
                                   map<tuple<string, uint64_t, int>, uint64_t> &,
                                   string, Function *);
        void handlePtrToStrctLocalVars(Module &,
                                       map<tuple<string, uint64_t, int>, uint64_t> &,
                                       string, Function *);
        void handlePtrToPrimitiveLocalVariables(
            Module &, map<uint64_t, pair<uint64_t, uint64_t>> &, string, Function *);
        void handleNestedStrct(
            Module &, map<tuple<string, string, uint64_t, uint64_t, int>, uint64_t> &,
            string, Function *);
        void handlePtrToNestedStrct(
            Module &, map<tuple<string, string, uint64_t, uint64_t, int>, uint64_t> &,
            string, Function *);

        LocalVariables(Module &module, string funcName)
        {
            logger.open("logger.txt", ofstream::app);
            instList = initalizePreNeckInstList(module, funcName);
        }
    };
} // namespace lmcas

#endif /* SOURCE_DIRECTORY__DEBLOAT_INCLUDE_LOCALVARIABLES_H_ */
