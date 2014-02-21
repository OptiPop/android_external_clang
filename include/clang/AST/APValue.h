//===--- APValue.h - Union class for APFloat/APSInt/Complex -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the APValue class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_APVALUE_H
#define LLVM_CLANG_AST_APVALUE_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"

#define FIX_ALIASING(tIN, tOUT, x) \
  union {\
    tIN *indata; \
    tOUT *outdata; \
  };\
  indata = (tIN*)x

#define FIX_ALIASING_GET(x) \
  FIX_ALIASING(char, x, Data)

#define FIX_ALIASING_GET_RETURN(x) \
  FIX_ALIASING_GET(x); \
  return *outdata

namespace clang {
  class AddrLabelExpr;
  class ASTContext;
  class CharUnits;
  class DiagnosticBuilder;
  class Expr;
  class FieldDecl;
  class Decl;
  class ValueDecl;
  class CXXRecordDecl;
  class QualType;

/// APValue - This class implements a discriminated union of [uninitialized]
/// [APSInt] [APFloat], [Complex APSInt] [Complex APFloat], [Expr + Offset],
/// [Vector: N * APValue], [Array: N * APValue]
class APValue {
  typedef llvm::APSInt APSInt;
  typedef llvm::APFloat APFloat;
public:
  enum ValueKind {
    Uninitialized,
    Int,
    Float,
    ComplexInt,
    ComplexFloat,
    LValue,
    Vector,
    Array,
    Struct,
    Union,
    MemberPointer,
    AddrLabelDiff
  };
  typedef llvm::PointerUnion<const ValueDecl *, const Expr *> LValueBase;
  typedef llvm::PointerIntPair<const Decl *, 1, bool> BaseOrMemberType;
  union LValuePathEntry {
    /// BaseOrMember - The FieldDecl or CXXRecordDecl indicating the next item
    /// in the path. An opaque value of type BaseOrMemberType.
    void *BaseOrMember;
    /// ArrayIndex - The array index of the next item in the path.
    uint64_t ArrayIndex;
  };
  struct NoLValuePath {};
  struct UninitArray {};
  struct UninitStruct {};
private:
  ValueKind Kind;

  struct ComplexAPSInt {
    APSInt Real, Imag;
    ComplexAPSInt() : Real(1), Imag(1) {}
  };
  struct ComplexAPFloat {
    APFloat Real, Imag;
    ComplexAPFloat() : Real(0.0), Imag(0.0) {}
  };
  struct LV;
  struct Vec {
    APValue *Elts;
    unsigned NumElts;
    Vec() : Elts(0), NumElts(0) {}
    ~Vec() { delete[] Elts; }
  };
  struct Arr {
    APValue *Elts;
    unsigned NumElts, ArrSize;
    Arr(unsigned NumElts, unsigned ArrSize);
    ~Arr();
  };
  struct StructData {
    APValue *Elts;
    unsigned NumBases;
    unsigned NumFields;
    StructData(unsigned NumBases, unsigned NumFields);
    ~StructData();
  };
  struct UnionData {
    const FieldDecl *Field;
    APValue *Value;
    UnionData();
    ~UnionData();
  };
  struct AddrLabelDiffData {
    const AddrLabelExpr* LHSExpr;
    const AddrLabelExpr* RHSExpr;
  };
  struct MemberPointerData;

  enum {
    MaxSize = (sizeof(ComplexAPSInt) > sizeof(ComplexAPFloat) ?
               sizeof(ComplexAPSInt) : sizeof(ComplexAPFloat))
  };

  union {
    void *Aligner;
    char Data[MaxSize];
  };

public:
  APValue() : Kind(Uninitialized) {}
  explicit APValue(const APSInt &I) : Kind(Uninitialized) {
    MakeInt(); setInt(I);
  }
  explicit APValue(const APFloat &F) : Kind(Uninitialized) {
    MakeFloat(); setFloat(F);
  }
  explicit APValue(const APValue *E, unsigned N) : Kind(Uninitialized) {
    MakeVector(); setVector(E, N);
  }
  APValue(const APSInt &R, const APSInt &I) : Kind(Uninitialized) {
    MakeComplexInt(); setComplexInt(R, I);
  }
  APValue(const APFloat &R, const APFloat &I) : Kind(Uninitialized) {
    MakeComplexFloat(); setComplexFloat(R, I);
  }
  APValue(const APValue &RHS);
  APValue(LValueBase B, const CharUnits &O, NoLValuePath N, unsigned CallIndex)
      : Kind(Uninitialized) {
    MakeLValue(); setLValue(B, O, N, CallIndex);
  }
  APValue(LValueBase B, const CharUnits &O, ArrayRef<LValuePathEntry> Path,
          bool OnePastTheEnd, unsigned CallIndex)
      : Kind(Uninitialized) {
    MakeLValue(); setLValue(B, O, Path, OnePastTheEnd, CallIndex);
  }
  APValue(UninitArray, unsigned InitElts, unsigned Size) : Kind(Uninitialized) {
    MakeArray(InitElts, Size);
  }
  APValue(UninitStruct, unsigned B, unsigned M) : Kind(Uninitialized) {
    MakeStruct(B, M);
  }
  explicit APValue(const FieldDecl *D, const APValue &V = APValue())
      : Kind(Uninitialized) {
    MakeUnion(); setUnion(D, V);
  }
  APValue(const ValueDecl *Member, bool IsDerivedMember,
          ArrayRef<const CXXRecordDecl*> Path) : Kind(Uninitialized) {
    MakeMemberPointer(Member, IsDerivedMember, Path);
  }
  APValue(const AddrLabelExpr* LHSExpr, const AddrLabelExpr* RHSExpr)
      : Kind(Uninitialized) {
    MakeAddrLabelDiff(); setAddrLabelDiff(LHSExpr, RHSExpr);
  }

  ~APValue() {
    MakeUninit();
  }

  /// \brief Returns whether the object performed allocations.
  ///
  /// If APValues are constructed via placement new, \c needsCleanup()
  /// indicates whether the destructor must be called in order to correctly
  /// free all allocated memory.
  bool needsCleanup() const;

  /// \brief Swaps the contents of this and the given APValue.
  void swap(APValue &RHS);

  ValueKind getKind() const { return Kind; }
  bool isUninit() const { return Kind == Uninitialized; }
  bool isInt() const { return Kind == Int; }
  bool isFloat() const { return Kind == Float; }
  bool isComplexInt() const { return Kind == ComplexInt; }
  bool isComplexFloat() const { return Kind == ComplexFloat; }
  bool isLValue() const { return Kind == LValue; }
  bool isVector() const { return Kind == Vector; }
  bool isArray() const { return Kind == Array; }
  bool isStruct() const { return Kind == Struct; }
  bool isUnion() const { return Kind == Union; }
  bool isMemberPointer() const { return Kind == MemberPointer; }
  bool isAddrLabelDiff() const { return Kind == AddrLabelDiff; }

  void dump() const;
  void dump(raw_ostream &OS) const;

  void printPretty(raw_ostream &OS, ASTContext &Ctx, QualType Ty) const;
  std::string getAsString(ASTContext &Ctx, QualType Ty) const;

  APSInt &getInt() {
    assert(isInt() && "Invalid accessor");
    FIX_ALIASING_GET_RETURN(APSInt);
  }
  const APSInt &getInt() const {
    return const_cast<APValue*>(this)->getInt();
  }

  APFloat &getFloat() {
    assert(isFloat() && "Invalid accessor");
    FIX_ALIASING_GET_RETURN(APFloat);
  }
  const APFloat &getFloat() const {
    return const_cast<APValue*>(this)->getFloat();
  }

  APSInt &getComplexIntReal() {
    assert(isComplexInt() && "Invalid accessor");
    FIX_ALIASING_GET(ComplexAPSInt);
    return outdata->Real;
  }
  const APSInt &getComplexIntReal() const {
    return const_cast<APValue*>(this)->getComplexIntReal();
  }

  APSInt &getComplexIntImag() {
    assert(isComplexInt() && "Invalid accessor");
    FIX_ALIASING_GET(ComplexAPSInt);
    return outdata->Imag;
  }
  const APSInt &getComplexIntImag() const {
    return const_cast<APValue*>(this)->getComplexIntImag();
  }

  APFloat &getComplexFloatReal() {
    assert(isComplexFloat() && "Invalid accessor");
    FIX_ALIASING_GET(ComplexAPFloat);
    return outdata->Real;
  }
  const APFloat &getComplexFloatReal() const {
    return const_cast<APValue*>(this)->getComplexFloatReal();
  }

  APFloat &getComplexFloatImag() {
    assert(isComplexFloat() && "Invalid accessor");
    FIX_ALIASING_GET(ComplexAPFloat);
    return outdata->Imag;
  }
  const APFloat &getComplexFloatImag() const {
    return const_cast<APValue*>(this)->getComplexFloatImag();
  }

  const LValueBase getLValueBase() const;
  CharUnits &getLValueOffset();
  const CharUnits &getLValueOffset() const {
    return const_cast<APValue*>(this)->getLValueOffset();
  }
  bool isLValueOnePastTheEnd() const;
  bool hasLValuePath() const;
  ArrayRef<LValuePathEntry> getLValuePath() const;
  unsigned getLValueCallIndex() const;

  APValue &getVectorElt(unsigned I) {
    assert(isVector() && "Invalid accessor");
    assert(I < getVectorLength() && "Index out of range");
    FIX_ALIASING_GET(Vec);
    return outdata->Elts[I];
  }
  const APValue &getVectorElt(unsigned I) const {
    return const_cast<APValue*>(this)->getVectorElt(I);
  }
  unsigned getVectorLength() const {
    assert(isVector() && "Invalid accessor");
    FIX_ALIASING(const void, const Vec, Data);
    return outdata->NumElts;
  }

  APValue &getArrayInitializedElt(unsigned I) {
    assert(isArray() && "Invalid accessor");
    assert(I < getArrayInitializedElts() && "Index out of range");
    FIX_ALIASING_GET(Arr);
    return outdata->Elts[I];
  }
  const APValue &getArrayInitializedElt(unsigned I) const {
    return const_cast<APValue*>(this)->getArrayInitializedElt(I);
  }
  bool hasArrayFiller() const {
    return getArrayInitializedElts() != getArraySize();
  }
  APValue &getArrayFiller() {
    assert(isArray() && "Invalid accessor");
    assert(hasArrayFiller() && "No array filler");
    FIX_ALIASING_GET(Arr);
    return outdata->Elts[getArrayInitializedElts()];
  }
  const APValue &getArrayFiller() const {
    return const_cast<APValue*>(this)->getArrayFiller();
  }
  unsigned getArrayInitializedElts() const {
    assert(isArray() && "Invalid accessor");
    FIX_ALIASING(const void, const Arr, Data);
    return outdata->NumElts;
  }
  unsigned getArraySize() const {
    assert(isArray() && "Invalid accessor");
    FIX_ALIASING(const void, const Arr, Data);
    return outdata->ArrSize;
  }

  unsigned getStructNumBases() const {
    assert(isStruct() && "Invalid accessor");
    FIX_ALIASING(const char, const StructData, Data);
    return outdata->NumBases;
  }
  unsigned getStructNumFields() const {
    assert(isStruct() && "Invalid accessor");
    FIX_ALIASING(const char, const StructData, Data);
    return outdata->NumFields;
  }
  APValue &getStructBase(unsigned i) {
    assert(isStruct() && "Invalid accessor");
    FIX_ALIASING_GET(StructData);
    return outdata->Elts[i];
  }
  APValue &getStructField(unsigned i) {
    assert(isStruct() && "Invalid accessor");
    FIX_ALIASING_GET(StructData);
    return outdata->Elts[getStructNumBases() + i];
  }
  const APValue &getStructBase(unsigned i) const {
    return const_cast<APValue*>(this)->getStructBase(i);
  }
  const APValue &getStructField(unsigned i) const {
    return const_cast<APValue*>(this)->getStructField(i);
  }

  const FieldDecl *getUnionField() const {
    assert(isUnion() && "Invalid accessor");
    FIX_ALIASING(const char, const UnionData, Data);    
    return outdata->Field;
  }
  APValue &getUnionValue() {
    assert(isUnion() && "Invalid accessor");
    FIX_ALIASING_GET(UnionData);
    return *outdata->Value;
  }
  const APValue &getUnionValue() const {
    return const_cast<APValue*>(this)->getUnionValue();
  }

  const ValueDecl *getMemberPointerDecl() const;
  bool isMemberPointerToDerivedMember() const;
  ArrayRef<const CXXRecordDecl*> getMemberPointerPath() const;

  const AddrLabelExpr* getAddrLabelDiffLHS() const {
    assert(isAddrLabelDiff() && "Invalid accessor");
    FIX_ALIASING(const char, const AddrLabelDiffData, Data);
    return outdata->LHSExpr;
  }
  const AddrLabelExpr* getAddrLabelDiffRHS() const {
    assert(isAddrLabelDiff() && "Invalid accessor");
    FIX_ALIASING(const char, const AddrLabelDiffData, Data);
    return outdata->RHSExpr;
  }

  void setInt(const APSInt &I) {
    assert(isInt() && "Invalid accessor");
    FIX_ALIASING_GET(APSInt);
    *outdata = I;
  }
  void setFloat(const APFloat &F) {
    assert(isFloat() && "Invalid accessor");
    FIX_ALIASING_GET(APFloat);
    *outdata = F;
  }
  void setVector(const APValue *E, unsigned N) {
    assert(isVector() && "Invalid accessor");
    FIX_ALIASING_GET(Vec);
    outdata->Elts = new APValue[N];
    outdata->NumElts = N;
    for (unsigned i = 0; i != N; ++i)
      outdata->Elts[i] = E[i];
  }
  void setComplexInt(const APSInt &R, const APSInt &I) {
    assert(R.getBitWidth() == I.getBitWidth() &&
           "Invalid complex int (type mismatch).");
    assert(isComplexInt() && "Invalid accessor");
    FIX_ALIASING_GET(ComplexAPSInt);
    outdata->Real = R;
    outdata->Imag = I;
  }
  void setComplexFloat(const APFloat &R, const APFloat &I) {
    assert(&R.getSemantics() == &I.getSemantics() &&
           "Invalid complex float (type mismatch).");
    assert(isComplexFloat() && "Invalid accessor");
    FIX_ALIASING_GET(ComplexAPFloat);
    outdata->Real = R;
    outdata->Imag = I;
  }
  void setLValue(LValueBase B, const CharUnits &O, NoLValuePath,
                 unsigned CallIndex);
  void setLValue(LValueBase B, const CharUnits &O,
                 ArrayRef<LValuePathEntry> Path, bool OnePastTheEnd,
                 unsigned CallIndex);
  void setUnion(const FieldDecl *Field, const APValue &Value) {
    assert(isUnion() && "Invalid accessor");
    FIX_ALIASING_GET(UnionData);
    outdata->Field = Field;
    *outdata->Value = Value;
  }
  void setAddrLabelDiff(const AddrLabelExpr* LHSExpr,
                        const AddrLabelExpr* RHSExpr) {
    FIX_ALIASING_GET(AddrLabelDiffData);
    outdata->LHSExpr = LHSExpr;
    outdata->RHSExpr = RHSExpr;
  }

  /// Assign by swapping from a copy of the RHS.
  APValue &operator=(APValue RHS) {
    swap(RHS);
    return *this;
  }

private:
  void DestroyDataAndMakeUninit();
  void MakeUninit() {
    if (Kind != Uninitialized)
      DestroyDataAndMakeUninit();
  }
  void MakeInt() {
    assert(isUninit() && "Bad state change");
    new ((void*)Data) APSInt(1);
    Kind = Int;
  }
  void MakeFloat() {
    assert(isUninit() && "Bad state change");
    new ((void*)(char*)Data) APFloat(0.0);
    Kind = Float;
  }
  void MakeVector() {
    assert(isUninit() && "Bad state change");
    new ((void*)(char*)Data) Vec();
    Kind = Vector;
  }
  void MakeComplexInt() {
    assert(isUninit() && "Bad state change");
    new ((void*)(char*)Data) ComplexAPSInt();
    Kind = ComplexInt;
  }
  void MakeComplexFloat() {
    assert(isUninit() && "Bad state change");
    new ((void*)(char*)Data) ComplexAPFloat();
    Kind = ComplexFloat;
  }
  void MakeLValue();
  void MakeArray(unsigned InitElts, unsigned Size);
  void MakeStruct(unsigned B, unsigned M) {
    assert(isUninit() && "Bad state change");
    new ((void*)(char*)Data) StructData(B, M);
    Kind = Struct;
  }
  void MakeUnion() {
    assert(isUninit() && "Bad state change");
    new ((void*)(char*)Data) UnionData();
    Kind = Union;
  }
  void MakeMemberPointer(const ValueDecl *Member, bool IsDerivedMember,
                         ArrayRef<const CXXRecordDecl*> Path);
  void MakeAddrLabelDiff() {
    assert(isUninit() && "Bad state change");
    new ((void*)(char*)Data) AddrLabelDiffData();
    Kind = AddrLabelDiff;
  }
};

} // end namespace clang.

#endif
