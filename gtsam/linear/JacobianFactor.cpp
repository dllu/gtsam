/* ----------------------------------------------------------------------------

 * GTSAM Copyright 2010, Georgia Tech Research Corporation,
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * Authors: Frank Dellaert, et al. (see THANKS for the full author list)

 * See LICENSE for the license information

 * -------------------------------------------------------------------------- */

/**
 * @file    JacobianFactor.cpp
 * @brief   
 * @author  Richard Roberts
 * @created Dec 8, 2010
 */

#include <gtsam/base/timing.h>
#include <gtsam/base/Matrix.h>
#include <gtsam/base/FastMap.h>
#include <gtsam/base/cholesky.h>
#include <gtsam/inference/VariableSlots.h>
#include <gtsam/inference/FactorGraph-inl.h>
#include <gtsam/linear/GaussianConditional.h>
#include <gtsam/linear/JacobianFactor.h>
#include <gtsam/linear/HessianFactor.h>
#include <gtsam/linear/GaussianFactorGraph.h>

#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/make_shared.hpp>
#include <boost/lambda/bind.hpp>
#include <boost/lambda/lambda.hpp>

#include <boost/numeric/ublas/triangular.hpp>
#include <boost/numeric/ublas/io.hpp>
#include <boost/numeric/ublas/matrix_proxy.hpp>
#include <boost/numeric/ublas/vector_proxy.hpp>
#include <boost/numeric/ublas/blas.hpp>

#include <sstream>
#include <stdexcept>

using namespace std;
namespace ublas = boost::numeric::ublas;
using namespace boost::lambda;

namespace gtsam {

  /* ************************************************************************* */
  inline void JacobianFactor::assertInvariants() const {
  #ifndef NDEBUG
    IndexFactor::assertInvariants();
    assert((keys_.size() == 0 && Ab_.size1() == 0 && Ab_.nBlocks() == 0) || keys_.size()+1 == Ab_.nBlocks());
    assert(firstNonzeroBlocks_.size() == Ab_.size1());
    for(size_t i=0; i<firstNonzeroBlocks_.size(); ++i)
      assert(firstNonzeroBlocks_[i] < Ab_.nBlocks());
  #endif

    // Check for non-finite values
    for(size_t i=0; i<Ab_.size1(); ++i)
      for(size_t j=0; j<Ab_.size2(); ++j)
        if(isnan(matrix_(i,j)))
          throw invalid_argument("JacobianFactor contains NaN matrix entries.");
  }

  /* ************************************************************************* */
  JacobianFactor::JacobianFactor(const JacobianFactor& gf) :
      GaussianFactor(gf), model_(gf.model_), firstNonzeroBlocks_(gf.firstNonzeroBlocks_), Ab_(matrix_) {
    Ab_.assignNoalias(gf.Ab_);
    assertInvariants();
  }

  /* ************************************************************************* */
  JacobianFactor::JacobianFactor() : Ab_(matrix_) { assertInvariants(); }

  /* ************************************************************************* */
  JacobianFactor::JacobianFactor(const Vector& b_in) : firstNonzeroBlocks_(b_in.size(), 0), Ab_(matrix_) {
    size_t dims[] = { 1 };
    Ab_.copyStructureFrom(BlockAb(matrix_, dims, dims+1, b_in.size()));
    getb() = b_in;
    assertInvariants();
  }

  /* ************************************************************************* */
  JacobianFactor::JacobianFactor(Index i1, const Matrix& A1,
      const Vector& b, const SharedDiagonal& model) :
      GaussianFactor(i1), model_(model), firstNonzeroBlocks_(b.size(), 0), Ab_(matrix_) {
    size_t dims[] = { A1.size2(), 1};
    Ab_.copyStructureFrom(BlockAb(matrix_, dims, dims+2, b.size()));
    Ab_(0) = A1;
    getb() = b;
    assertInvariants();
  }

  /* ************************************************************************* */
  JacobianFactor::JacobianFactor(Index i1, const Matrix& A1, Index i2, const Matrix& A2,
      const Vector& b, const SharedDiagonal& model) :
      GaussianFactor(i1,i2), model_(model), firstNonzeroBlocks_(b.size(), 0), Ab_(matrix_) {
    size_t dims[] = { A1.size2(), A2.size2(), 1};
    Ab_.copyStructureFrom(BlockAb(matrix_, dims, dims+3, b.size()));
    Ab_(0) = A1;
    Ab_(1) = A2;
    getb() = b;
    assertInvariants();
  }

  /* ************************************************************************* */
  JacobianFactor::JacobianFactor(Index i1, const Matrix& A1, Index i2, const Matrix& A2,
      Index i3, const Matrix& A3, const Vector& b, const SharedDiagonal& model) :
      GaussianFactor(i1,i2,i3), model_(model), firstNonzeroBlocks_(b.size(), 0), Ab_(matrix_) {
    size_t dims[] = { A1.size2(), A2.size2(), A3.size2(), 1};
    Ab_.copyStructureFrom(BlockAb(matrix_, dims, dims+4, b.size()));
    Ab_(0) = A1;
    Ab_(1) = A2;
    Ab_(2) = A3;
    getb() = b;
    assertInvariants();
  }

  /* ************************************************************************* */
  JacobianFactor::JacobianFactor(const std::vector<std::pair<Index, Matrix> > &terms,
      const Vector &b, const SharedDiagonal& model) :
      model_(model), firstNonzeroBlocks_(b.size(), 0), Ab_(matrix_) {
    keys_.resize(terms.size());
    size_t dims[terms.size()+1];
    for(size_t j=0; j<terms.size(); ++j) {
      keys_[j] = terms[j].first;
      dims[j] = terms[j].second.size2();
    }
    dims[terms.size()] = 1;
    Ab_.copyStructureFrom(BlockAb(matrix_, dims, dims+terms.size()+1, b.size()));
    for(size_t j=0; j<terms.size(); ++j)
      Ab_(j) = terms[j].second;
    getb() = b;
    assertInvariants();
  }

  /* ************************************************************************* */
  JacobianFactor::JacobianFactor(const std::list<std::pair<Index, Matrix> > &terms,
      const Vector &b, const SharedDiagonal& model) :
      model_(model), firstNonzeroBlocks_(b.size(), 0), Ab_(matrix_) {
    keys_.resize(terms.size());
    size_t dims[terms.size()+1];
    size_t j=0;
    for(std::list<std::pair<Index, Matrix> >::const_iterator term=terms.begin(); term!=terms.end(); ++term) {
      keys_[j] = term->first;
      dims[j] = term->second.size2();
      ++ j;
    }
    dims[j] = 1;
    firstNonzeroBlocks_.resize(b.size(), 0);
    Ab_.copyStructureFrom(BlockAb(matrix_, dims, dims+terms.size()+1, b.size()));
    j = 0;
    for(std::list<std::pair<Index, Matrix> >::const_iterator term=terms.begin(); term!=terms.end(); ++term) {
      Ab_(j) = term->second;
      ++ j;
    }
    getb() = b;
    assertInvariants();
  }

  /* ************************************************************************* */
  JacobianFactor::JacobianFactor(const GaussianConditional& cg) : GaussianFactor(cg), model_(noiseModel::Diagonal::Sigmas(cg.get_sigmas(), true)), Ab_(matrix_) {
    Ab_.assignNoalias(cg.rsd_);
    // todo SL: make firstNonzeroCols triangular?
    firstNonzeroBlocks_.resize(cg.get_d().size(), 0); // set sigmas from precisions
    assertInvariants();
  }

  /* ************************************************************************* */
  JacobianFactor::JacobianFactor(const HessianFactor& factor) : Ab_(matrix_) {
    keys_ = factor.keys_;
    Ab_.assignNoalias(factor.info_);
    size_t maxrank = choleskyCareful(matrix_).first;
    matrix_ = ublas::triangular_adaptor<AbMatrix, ublas::upper>(matrix_);
    Ab_.rowEnd() = maxrank;
    model_ = noiseModel::Unit::Create(maxrank);

    size_t varpos = 0;
    firstNonzeroBlocks_.resize(this->size1(), 0);

    // Sort keys
    set<Index> vars;
    for(size_t j=0; j<keys_.size(); ++j)
      vars.insert(keys_[j]);
    Permutation permutation(Permutation::Identity(*vars.rbegin() + 1));
    size_t jNew = 0;
    BOOST_FOREACH(const Index& var, vars) {
      permutation[var] = jNew++;
    }
    permuteWithInverse(permutation);
    jNew = 0;
    BOOST_FOREACH(const Index& var, vars) {
      keys_[jNew++] = var;
    }

    assertInvariants();
  }

  /* ************************************************************************* */
  void JacobianFactor::print(const string& s) const {
    cout << s << "\n";
    if (empty()) {
      cout << " empty, keys: ";
      BOOST_FOREACH(const Index key, keys_) { cout << key << " "; }
      cout << endl;
    } else {
      for(const_iterator key=begin(); key!=end(); ++key)
        gtsam::print(getA(key), (boost::format("A[%1%]=\n")%*key).str());
      gtsam::print(getb(),"b=");
      model_->print("model");
    }
  }

  /* ************************************************************************* */
  // Check if two linear factors are equal
  bool JacobianFactor::equals(const GaussianFactor& f_, double tol) const {
    if(!dynamic_cast<const JacobianFactor*>(&f_))
      return false;
    else {
      const JacobianFactor& f(static_cast<const JacobianFactor&>(f_));
      if (empty()) return (f.empty());
      if(keys_!=f.keys_ /*|| !model_->equals(lf->model_, tol)*/)
        return false;

      assert(Ab_.size1() == f.Ab_.size1() && Ab_.size2() == f.Ab_.size2());

      constABlock Ab1(Ab_.range(0, Ab_.nBlocks()));
      constABlock Ab2(f.Ab_.range(0, f.Ab_.nBlocks()));
      for(size_t row=0; row<Ab1.size1(); ++row)
        if(!equal_with_abs_tol(ublas::row(Ab1, row), ublas::row(Ab2, row), tol) &&
            !equal_with_abs_tol(-ublas::row(Ab1, row), ublas::row(Ab2, row), tol))
          return false;

      return true;
    }
  }

  /* ************************************************************************* */
  void JacobianFactor::permuteWithInverse(const Permutation& inversePermutation) {

    // Build a map from the new variable indices to the old slot positions.
    typedef map<size_t, size_t, std::less<size_t>, boost::fast_pool_allocator<std::pair<const size_t, size_t> > > SourceSlots;
    SourceSlots sourceSlots;
    for(size_t j=0; j<keys_.size(); ++j)
      sourceSlots.insert(make_pair(inversePermutation[keys_[j]], j));

    // Build a vector of variable dimensions in the new order
    vector<size_t> dimensions(keys_.size() + 1);
    size_t j = 0;
    BOOST_FOREACH(const SourceSlots::value_type& sourceSlot, sourceSlots) {
      dimensions[j++] = Ab_(sourceSlot.second).size2();
    }
    assert(j == keys_.size());
    dimensions.back() = 1;

    // Copy the variables and matrix into the new order
    vector<Index> oldKeys(keys_.size());
    keys_.swap(oldKeys);
    AbMatrix oldMatrix;
    BlockAb oldAb(oldMatrix, dimensions.begin(), dimensions.end(), Ab_.size1());
    Ab_.swap(oldAb);
    j = 0;
    BOOST_FOREACH(const SourceSlots::value_type& sourceSlot, sourceSlots) {
      keys_[j] = sourceSlot.first;
      ublas::noalias(Ab_(j++)) = oldAb(sourceSlot.second);
    }
    ublas::noalias(Ab_(j)) = oldAb(j);

    // Since we're permuting the variables, ensure that entire rows from this
    // factor are copied when Combine is called
    BOOST_FOREACH(size_t& varpos, firstNonzeroBlocks_) { varpos = 0; }
    assertInvariants();
  }

  /* ************************************************************************* */
  Vector JacobianFactor::unweighted_error(const VectorValues& c) const {
    Vector e = -getb();
    if (empty()) return e;
    for(size_t pos=0; pos<keys_.size(); ++pos)
      e += ublas::prod(Ab_(pos), c[keys_[pos]]);
    return e;
  }

  /* ************************************************************************* */
  Vector JacobianFactor::error_vector(const VectorValues& c) const {
    if (empty()) return model_->whiten(-getb());
    return model_->whiten(unweighted_error(c));
  }

  /* ************************************************************************* */
  double JacobianFactor::error(const VectorValues& c) const {
    if (empty()) return 0;
    Vector weighted = error_vector(c);
    return 0.5 * inner_prod(weighted,weighted);
  }


  /* ************************************************************************* */
  Vector JacobianFactor::operator*(const VectorValues& x) const {
    Vector Ax = zero(Ab_.size1());
    if (empty()) return Ax;

    // Just iterate over all A matrices and multiply in correct config part
    for(size_t pos=0; pos<keys_.size(); ++pos)
      Ax += ublas::prod(Ab_(pos), x[keys_[pos]]);

    return model_->whiten(Ax);
  }


  /* ************************************************************************* */
  void JacobianFactor::transposeMultiplyAdd(double alpha, const Vector& e,
      VectorValues& x) const {
    Vector E = alpha * model_->whiten(e);
    // Just iterate over all A matrices and insert Ai^e into VectorValues
    for(size_t pos=0; pos<keys_.size(); ++pos)
      gtsam::transposeMultiplyAdd(1.0, Ab_(pos), E, x[keys_[pos]]);
  }

  /* ************************************************************************* */
  pair<Matrix,Vector> JacobianFactor::matrix(bool weight) const {
    Matrix A(Ab_.range(0, keys_.size()));
    Vector b(getb());
    // divide in sigma so error is indeed 0.5*|Ax-b|
    if (weight) model_->WhitenSystem(A,b);
    return make_pair(A, b);
  }

  /* ************************************************************************* */
  Matrix JacobianFactor::matrix_augmented(bool weight) const {
    if (weight) { Matrix Ab(Ab_.range(0,Ab_.nBlocks())); model_->WhitenInPlace(Ab); return Ab; }
    else return Ab_.range(0, Ab_.nBlocks());
  }

  /* ************************************************************************* */
  boost::tuple<list<int>, list<int>, list<double> >
  JacobianFactor::sparse(const map<Index,size_t>& columnIndices) const {

    // declare return values
    list<int> I,J;
    list<double> S;

    // iterate over all matrices in the factor
    for(size_t pos=0; pos<keys_.size(); ++pos) {
      constABlock A(Ab_(pos));
      // find first column index for this key
      int column_start = columnIndices.at(keys_[pos]);
      for (size_t i = 0; i < A.size1(); i++) {
        double sigma_i = model_->sigma(i);
        for (size_t j = 0; j < A.size2(); j++)
          if (A(i, j) != 0.0) {
            I.push_back(i + 1);
            J.push_back(j + column_start);
            S.push_back(A(i, j) / sigma_i);
          }
      }
    }

    // return the result
    return boost::tuple<list<int>, list<int>, list<double> >(I,J,S);
  }

  /* ************************************************************************* */
  JacobianFactor JacobianFactor::whiten() const {
    JacobianFactor result(*this);
    result.model_->WhitenInPlace(result.matrix_);
    result.model_ = noiseModel::Unit::Create(result.model_->dim());
    return result;
  }

  /* ************************************************************************* */
  GaussianConditional::shared_ptr JacobianFactor::eliminateFirst() {
    return this->eliminate(1)->front();
  }

  /* ************************************************************************* */
  GaussianBayesNet::shared_ptr JacobianFactor::eliminate(size_t nrFrontals) {

    assert(Ab_.rowStart() == 0 && Ab_.rowEnd() == matrix_.size1() && Ab_.firstBlock() == 0);
    assert(keys_.size() >= nrFrontals);
    assertInvariants();

    static const bool debug = false;

    if(debug) cout << "Eliminating " << nrFrontals << " frontal variables" << endl;
    if(debug) this->print("Eliminating JacobianFactor: ");

    tic(1, "stairs");
    // Translate the left-most nonzero column indices into top-most zero row indices
    vector<int> firstZeroRows(Ab_.size2());
    {
      size_t lastNonzeroRow = 0;
      vector<int>::iterator firstZeroRowsIt = firstZeroRows.begin();
      for(size_t var=0; var<keys().size(); ++var) {
        while(lastNonzeroRow < this->size1() && firstNonzeroBlocks_[lastNonzeroRow] <= var)
          ++ lastNonzeroRow;
        fill(firstZeroRowsIt, firstZeroRowsIt+Ab_(var).size2(), lastNonzeroRow);
        firstZeroRowsIt += Ab_(var).size2();
      }
      assert(firstZeroRowsIt+1 == firstZeroRows.end());
      *firstZeroRowsIt = this->size1();
    }
    toc(1, "stairs");

  #ifndef NDEBUG
    for(size_t col=0; col<Ab_.size2(); ++col) {
      if(debug) cout << "Staircase[" << col << "] = " << firstZeroRows[col] << endl;
      if(col != 0) assert(firstZeroRows[col] >= firstZeroRows[col-1]);
      assert(firstZeroRows[col] <= (long)this->size1());
    }
  #endif

    if(debug) gtsam::print(matrix_, "Augmented Ab: ");

    size_t frontalDim = Ab_.range(0,nrFrontals).size2();

    if(debug) cout << "frontalDim = " << frontalDim << endl;

    // Use in-place QR or Cholesky on dense Ab appropriate to NoiseModel
    tic(2, "QR");
    SharedDiagonal noiseModel = model_->QRColumnWise(matrix_, firstZeroRows);
    toc(2, "QR");

    // Zero the lower-left triangle.  todo: not all of these entries actually
    // need to be zeroed if we are careful to start copying rows after the last
    // structural zero.
    if(matrix_.size1() > 0) {
      for(size_t j=0; j<matrix_.size2(); ++j)
        for(size_t i=j+1; i<noiseModel->dim(); ++i)
          matrix_(i,j) = 0.0;
    }

    if(debug) gtsam::print(matrix_, "QR result: ");
    if(debug) noiseModel->print("QR result noise model: ");

    // Check for singular factor
    if(noiseModel->dim() < frontalDim) {
      throw domain_error((boost::format(
          "JacobianFactor is singular in variable %1%, discovered while attempting\n"
          "to eliminate this variable.") % keys_.front()).str());
    }

    // Extract conditionals
    tic(3, "cond Rd");
    GaussianBayesNet::shared_ptr conditionals(new GaussianBayesNet());
    for(size_t j=0; j<nrFrontals; ++j) {
      // Temporarily restrict the matrix view to the conditional blocks of the
      // eliminated Ab matrix to create the GaussianConditional from it.
      size_t varDim = Ab_(0).size2();
      Ab_.rowEnd() = Ab_.rowStart() + varDim;
      const ublas::vector_range<const Vector> sigmas(noiseModel->sigmas(), ublas::range(Ab_.rowStart(), Ab_.rowEnd()));
      conditionals->push_back(boost::make_shared<Conditional>(keys_.begin()+j, keys_.end(), 1, Ab_, sigmas));
      if(debug) conditionals->back()->print("Extracted conditional: ");
      Ab_.rowStart() += varDim;
      Ab_.firstBlock() += 1;
    }
    toc(3, "cond Rd");

    if(debug) conditionals->print("Extracted conditionals: ");

    tic(4, "remaining factor");
    // Take lower-right block of Ab to get the new factor
    Ab_.rowEnd() = noiseModel->dim();
    keys_.assign(keys_.begin() + nrFrontals, keys_.end());
    // Set sigmas with the right model
    if (noiseModel->isConstrained())
      model_ = noiseModel::Constrained::MixedSigmas(sub(noiseModel->sigmas(), frontalDim, noiseModel->dim()));
    else
      model_ = noiseModel::Diagonal::Sigmas(sub(noiseModel->sigmas(), frontalDim, noiseModel->dim()));
    if(debug) this->print("Eliminated factor: ");
    assert(Ab_.size1() <= Ab_.size2()-1);
    toc(4, "remaining factor");

    // todo SL: deal with "dead" pivot columns!!!
    tic(5, "rowstarts");
    size_t varpos = 0;
    firstNonzeroBlocks_.resize(this->size1());
    for(size_t row=0; row<size1(); ++row) {
      if(debug) cout << "row " << row << " varpos " << varpos << " Ab_.offset(varpos)=" << Ab_.offset(varpos) << " Ab_.offset(varpos+1)=" << Ab_.offset(varpos+1) << endl;
      while(varpos < this->keys_.size() && Ab_.offset(varpos+1) <= row)
        ++ varpos;
      firstNonzeroBlocks_[row] = varpos;
      if(debug) cout << "firstNonzeroVars_[" << row << "] = " << firstNonzeroBlocks_[row] << endl;
    }
    toc(5, "rowstarts");

    if(debug) print("Eliminated factor: ");

    assertInvariants();

    return conditionals;

  }

  /* ************************************************************************* */
  /* Used internally by JacobianFactor::Combine for sorting */
  struct _RowSource {
    size_t firstNonzeroVar;
    size_t factorI;
    size_t factorRowI;
    _RowSource(size_t _firstNonzeroVar, size_t _factorI, size_t _factorRowI) :
      firstNonzeroVar(_firstNonzeroVar), factorI(_factorI), factorRowI(_factorRowI) {}
    bool operator<(const _RowSource& o) const { return firstNonzeroVar < o.firstNonzeroVar; }
  };

  /* ************************************************************************* */
  // Helper functions for Combine
  static boost::tuple<vector<size_t>, size_t, size_t> countDims(const std::vector<JacobianFactor::shared_ptr>& factors, const VariableSlots& variableSlots) {
  #ifndef NDEBUG
    vector<size_t> varDims(variableSlots.size(), numeric_limits<size_t>::max());
  #else
    vector<size_t> varDims(variableSlots.size());
  #endif
    size_t m = 0;
    size_t n = 0;
    {
      Index jointVarpos = 0;
      BOOST_FOREACH(const VariableSlots::value_type& slots, variableSlots) {

        assert(slots.second.size() == factors.size());

        Index sourceFactorI = 0;
        BOOST_FOREACH(const Index sourceVarpos, slots.second) {
          if(sourceVarpos < numeric_limits<Index>::max()) {
            const JacobianFactor& sourceFactor = *factors[sourceFactorI];
            size_t vardim = sourceFactor.getDim(sourceFactor.begin() + sourceVarpos);
  #ifndef NDEBUG
            if(varDims[jointVarpos] == numeric_limits<size_t>::max()) {
              varDims[jointVarpos] = vardim;
              n += vardim;
            } else
              assert(varDims[jointVarpos] == vardim);
  #else
            varDims[jointVarpos] = vardim;
            n += vardim;
            break;
  #endif
          }
          ++ sourceFactorI;
        }
        ++ jointVarpos;
      }
      BOOST_FOREACH(const JacobianFactor::shared_ptr& factor, factors) {
        m += factor->size1();
      }
    }
    return boost::make_tuple(varDims, m, n);
  }

  /* ************************************************************************* */
  JacobianFactor::shared_ptr JacobianFactor::Combine(const FactorGraph<JacobianFactor>& factors, const VariableSlots& variableSlots) {

    static const bool debug = false;

    if(debug) factors.print("Combining factors: ");

    if(debug) variableSlots.print();

    // Determine dimensions
    tic(1, "countDims");
    vector<size_t> varDims;
    size_t m;
    size_t n;
    boost::tie(varDims, m, n) = countDims(factors, variableSlots);
    if(debug) {
      cout << "Dims: " << m << " x " << n << "\n";
      BOOST_FOREACH(const size_t dim, varDims) { cout << dim << " "; }
      cout << endl;
    }
    toc(1, "countDims");

    // Sort rows
    tic(2, "sort rows");
    vector<_RowSource> rowSources; rowSources.reserve(m);
    bool anyConstrained = false;
    for(size_t sourceFactorI = 0; sourceFactorI < factors.size(); ++sourceFactorI) {
      const JacobianFactor& sourceFactor(*factors[sourceFactorI]);
      sourceFactor.assertInvariants();
      for(size_t sourceFactorRow = 0; sourceFactorRow < sourceFactor.size1(); ++sourceFactorRow) {
        Index firstNonzeroVar;
        if(sourceFactor.firstNonzeroBlocks_[sourceFactorRow] < sourceFactor.size())
          firstNonzeroVar = sourceFactor.keys_[sourceFactor.firstNonzeroBlocks_[sourceFactorRow]];
        else if(sourceFactor.firstNonzeroBlocks_[sourceFactorRow] == sourceFactor.size())
          firstNonzeroVar = sourceFactor.back() + 1;
        else
          assert(false);
        rowSources.push_back(_RowSource(firstNonzeroVar, sourceFactorI, sourceFactorRow));
      }
      if(sourceFactor.model_->isConstrained()) anyConstrained = true;
    }
    assert(rowSources.size() == m);
    std::sort(rowSources.begin(), rowSources.end());
    toc(2, "sort rows");

    // Allocate new factor
    tic(3, "allocate");
    shared_ptr combined(new JacobianFactor());
    combined->keys_.resize(variableSlots.size());
    std::transform(variableSlots.begin(), variableSlots.end(), combined->keys_.begin(), bind(&VariableSlots::const_iterator::value_type::first, boost::lambda::_1));
    varDims.push_back(1);
    combined->Ab_.copyStructureFrom(BlockAb(combined->matrix_, varDims.begin(), varDims.end(), m));
    combined->firstNonzeroBlocks_.resize(m);
    Vector sigmas(m);
    toc(3, "allocate");

    // Copy rows
    tic(4, "copy rows");
    Index combinedSlot = 0;
    BOOST_FOREACH(const VariableSlots::value_type& varslot, variableSlots) {
      for(size_t row = 0; row < m; ++row) {
        const Index sourceSlot = varslot.second[rowSources[row].factorI];
        ABlock combinedBlock(combined->Ab_(combinedSlot));
        if(sourceSlot != numeric_limits<Index>::max()) {
          const JacobianFactor& source(*factors[rowSources[row].factorI]);
          const size_t sourceRow = rowSources[row].factorRowI;
          if(source.firstNonzeroBlocks_[sourceRow] <= sourceSlot) {
            const constABlock sourceBlock(source.Ab_(sourceSlot));
            ublas::noalias(ublas::row(combinedBlock, row)) = ublas::row(sourceBlock, sourceRow);
          } else
            ublas::noalias(ublas::row(combinedBlock, row)) = ublas::zero_vector<double>(combinedBlock.size2());
        } else
          ublas::noalias(ublas::row(combinedBlock, row)) = ublas::zero_vector<double>(combinedBlock.size2());
      }
      ++ combinedSlot;
    }
    toc(4, "copy rows");

    // Copy rhs (b), sigma, and firstNonzeroBlocks
    tic(5, "copy vectors");
    Index firstNonzeroSlot = 0;
    for(size_t row = 0; row < m; ++row) {
      const JacobianFactor& source(*factors[rowSources[row].factorI]);
      const size_t sourceRow = rowSources[row].factorRowI;
      combined->getb()(row) = source.getb()(sourceRow);
      sigmas(row) = source.get_model()->sigmas()(sourceRow);
      while(firstNonzeroSlot < variableSlots.size() && rowSources[row].firstNonzeroVar > combined->keys_[firstNonzeroSlot])
        ++ firstNonzeroSlot;
      combined->firstNonzeroBlocks_[row] = firstNonzeroSlot;
    }
    toc(5, "copy vectors");

    // Create noise model from sigmas
    tic(6, "noise model");
    if(anyConstrained) combined->model_ = noiseModel::Constrained::MixedSigmas(sigmas);
    else combined->model_ = noiseModel::Diagonal::Sigmas(sigmas);
    toc(6, "noise model");

    combined->assertInvariants();

    return combined;
  }

  /* ************************************************************************* */
  pair<GaussianBayesNet::shared_ptr, JacobianFactor::shared_ptr> JacobianFactor::CombineAndEliminate(
      const FactorGraph<JacobianFactor>& factors, size_t nrFrontals) {
    tic(1, "Combine");
    shared_ptr jointFactor(Combine(factors, VariableSlots(factors)));
    toc(1, "Combine");
    tic(2, "eliminate");
    GaussianBayesNet::shared_ptr gbn(jointFactor->eliminate(nrFrontals));
    toc(2, "eliminate");
    return make_pair(gbn, jointFactor);
  }


  /* ************************************************************************* */
  Errors operator*(const FactorGraph<JacobianFactor>& fg, const VectorValues& x) {
    Errors e;
    BOOST_FOREACH(const JacobianFactor::shared_ptr& Ai, fg) {
      e.push_back((*Ai)*x);
    }
    return e;
  }

  /* ************************************************************************* */
  void multiplyInPlace(const FactorGraph<JacobianFactor>& fg, const VectorValues& x, Errors& e) {
    multiplyInPlace(fg,x,e.begin());
  }

  /* ************************************************************************* */
  void multiplyInPlace(const FactorGraph<JacobianFactor>& fg, const VectorValues& x, const Errors::iterator& e) {
    Errors::iterator ei = e;
    BOOST_FOREACH(const JacobianFactor::shared_ptr& Ai, fg) {
      *ei = (*Ai)*x;
      ei++;
    }
  }


  /* ************************************************************************* */
  // x += alpha*A'*e
  void transposeMultiplyAdd(const FactorGraph<JacobianFactor>& fg, double alpha, const Errors& e, VectorValues& x) {
    // For each factor add the gradient contribution
    Errors::const_iterator ei = e.begin();
    BOOST_FOREACH(const JacobianFactor::shared_ptr& Ai, fg) {
      Ai->transposeMultiplyAdd(alpha,*(ei++),x);
    }
  }

  /* ************************************************************************* */
  VectorValues gradient(const FactorGraph<JacobianFactor>& fg, const VectorValues& x) {
    // It is crucial for performance to make a zero-valued clone of x
    VectorValues g = VectorValues::zero(x);
    Errors e;
    BOOST_FOREACH(const JacobianFactor::shared_ptr& factor, fg) {
      e.push_back(factor->error_vector(x));
    }
    transposeMultiplyAdd(fg, 1.0, e, g);
    return g;
  }

  /* ************************************************************************* */
  void residual(const FactorGraph<JacobianFactor>& fg, const VectorValues &x, VectorValues &r) {
    Index i = 0 ;
    BOOST_FOREACH(const JacobianFactor::shared_ptr& factor, fg) {
      r[i] = factor->getb();
      i++;
    }
    VectorValues Ax = VectorValues::SameStructure(r);
    multiply(fg,x,Ax);
    axpy(-1.0,Ax,r);
  }

  /* ************************************************************************* */
  void multiply(const FactorGraph<JacobianFactor>& fg, const VectorValues &x, VectorValues &r) {
    r.makeZero();
    Index i = 0;
    BOOST_FOREACH(const JacobianFactor::shared_ptr& factor, fg) {
      for(JacobianFactor::const_iterator j = factor->begin(); j != factor->end(); ++j) {
        r[i] += prod(factor->getA(j), x[*j]);
      }
      ++i;
    }
  }

  /* ************************************************************************* */
  void transposeMultiply(const FactorGraph<JacobianFactor>& fg, const VectorValues &r, VectorValues &x) {
    x.makeZero();
    Index i = 0;
    BOOST_FOREACH(const JacobianFactor::shared_ptr& factor, fg) {
      for(JacobianFactor::const_iterator j = factor->begin(); j != factor->end(); ++j) {
        x[*j] += prod(trans(factor->getA(j)), r[i]);
      }
      ++i;
    }
  }

}
