/* ----------------------------------------------------------------------------

 * GTSAM Copyright 2010, Georgia Tech Research Corporation,
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * Authors: Frank Dellaert, et al. (see THANKS for the full author list)

 * See LICENSE for the license information

 * -------------------------------------------------------------------------- */

/**
 * @file    JacobianFactor.h
 * @brief   
 * @author  Richard Roberts
 * @created Dec 8, 2010
 */
#pragma once

#include <gtsam/base/types.h>
#include <gtsam/base/Matrix.h>
#include <gtsam/base/Vector.h>
#include <gtsam/linear/GaussianFactor.h>
#include <gtsam/linear/SharedDiagonal.h>
#include <gtsam/linear/Errors.h>

#include <map>
#include <boost/tuple/tuple.hpp>

// Forward declarations of friend unit tests
class Combine2GaussianFactorTest;
class eliminateFrontalsGaussianFactorTest;

namespace gtsam {

  // Forward declarations
  class HessianFactor;
  class VariableSlots;

  class JacobianFactor : public GaussianFactor {

  protected:
    typedef MatrixColMajor AbMatrix;
    typedef VerticalBlockView<AbMatrix> BlockAb;

    SharedDiagonal model_; // Gaussian noise model with diagonal covariance matrix
    std::vector<size_t> firstNonzeroBlocks_;
    AbMatrix matrix_; // the full matrix corresponding to the factor
    BlockAb Ab_;      // the block view of the full matrix

  public:
    typedef boost::shared_ptr<JacobianFactor> shared_ptr;
    typedef BlockAb::Block ABlock;
    typedef BlockAb::constBlock constABlock;
    typedef BlockAb::Column BVector;
    typedef BlockAb::constColumn constBVector;

  protected:
    void assertInvariants() const;
    static JacobianFactor::shared_ptr Combine(const FactorGraph<JacobianFactor>& factors, const VariableSlots& variableSlots);

  public:

    /** Copy constructor */
    JacobianFactor(const JacobianFactor& gf);

    /** default constructor for I/O */
    JacobianFactor();

    /** Construct Null factor */
    JacobianFactor(const Vector& b_in);

    /** Construct unary factor */
    JacobianFactor(Index i1, const Matrix& A1,
        const Vector& b, const SharedDiagonal& model);

    /** Construct binary factor */
    JacobianFactor(Index i1, const Matrix& A1,
        Index i2, const Matrix& A2,
        const Vector& b, const SharedDiagonal& model);

    /** Construct ternary factor */
    JacobianFactor(Index i1, const Matrix& A1, Index i2,
        const Matrix& A2, Index i3, const Matrix& A3,
        const Vector& b, const SharedDiagonal& model);

    /** Construct an n-ary factor */
    JacobianFactor(const std::vector<std::pair<Index, Matrix> > &terms,
        const Vector &b, const SharedDiagonal& model);

    JacobianFactor(const std::list<std::pair<Index, Matrix> > &terms,
        const Vector &b, const SharedDiagonal& model);

    /** Construct from Conditional Gaussian */
    JacobianFactor(const GaussianConditional& cg);

    /** Convert from a HessianFactor (does Cholesky) */
    JacobianFactor(const HessianFactor& factor);

    virtual ~JacobianFactor() {}

    // Implementing Testable interface
    virtual void print(const std::string& s = "") const;
    virtual bool equals(const GaussianFactor& lf, double tol = 1e-9) const;

    Vector unweighted_error(const VectorValues& c) const; /** (A*x-b) */
    Vector error_vector(const VectorValues& c) const; /** (A*x-b)/sigma */
    virtual double error(const VectorValues& c) const; /**  0.5*(A*x-b)'*D*(A*x-b) */

    /** Check if the factor contains no information, i.e. zero rows.  This does
     * not necessarily mean that the factor involves no variables (to check for
     * involving no variables use keys().empty()).
     */
    bool empty() const { return Ab_.size1() == 0;}

    /** Return the dimension of the variable pointed to by the given key iterator
     * todo: Remove this in favor of keeping track of dimensions with variables?
     */
    virtual size_t getDim(const_iterator variable) const { return Ab_(variable - keys_.begin()).size2(); }

    /**
     * Permutes the GaussianFactor, but for efficiency requires the permutation
     * to already be inverted.  This acts just as a change-of-name for each
     * variable.  The order of the variables within the factor is not changed.
     */
    virtual void permuteWithInverse(const Permutation& inversePermutation);

    /**
     * return the number of rows in the corresponding linear system
     */
    size_t size1() const { return Ab_.size1(); }

    /**
     * return the number of columns in the corresponding linear system
     */
    size_t size2() const { return Ab_.size2(); }

    /** get a copy of model */
    const SharedDiagonal& get_model() const { return model_;  }

    /** Get a view of the r.h.s. vector b */
    constBVector getb() const { return Ab_.column(size(), 0); }

    /** Get a view of the A matrix for the variable pointed to be the given key iterator */
    constABlock getA(const_iterator variable) const { return Ab_(variable - keys_.begin()); }

    BVector getb() { return Ab_.column(size(), 0); }

    ABlock getA(iterator variable) { return Ab_(variable - keys_.begin()); }

    /** Return A*x */
    Vector operator*(const VectorValues& x) const;

    /** x += A'*e */
    void transposeMultiplyAdd(double alpha, const Vector& e, VectorValues& x) const;

    /**
     * Return (dense) matrix associated with factor
     * @param ordering of variables needed for matrix column order
     * @param set weight to true to bake in the weights
     */
    std::pair<Matrix, Vector> matrix(bool weight = true) const;

    /**
     * Return (dense) matrix associated with factor
     * The returned system is an augmented matrix: [A b]
     * @param ordering of variables needed for matrix column order
     * @param set weight to use whitening to bake in weights
     */
    Matrix matrix_augmented(bool weight = true) const;

    /**
     * Return vectors i, j, and s to generate an m-by-n sparse matrix
     * such that S(i(k),j(k)) = s(k), which can be given to MATLAB's sparse.
     * As above, the standard deviations are baked into A and b
     * @param first column index for each variable
     */
    boost::tuple<std::list<int>, std::list<int>, std::list<double> >
    sparse(const std::map<Index, size_t>& columnIndices) const;

    /**
     * Return a whitened version of the factor, i.e. with unit diagonal noise
     * model. */
    JacobianFactor whiten() const;

    /**
     * Combine and eliminate several factors.
     */
    static std::pair<GaussianBayesNet::shared_ptr, shared_ptr> CombineAndEliminate(
        const FactorGraph<JacobianFactor>& factors, size_t nrFrontals=1);

    GaussianConditional::shared_ptr eliminateFirst();

    GaussianBayesNet::shared_ptr eliminate(size_t nrFrontals = 1);

    // Friend HessianFactor to facilitate convertion constructors
    friend class HessianFactor;

    // Friend unit tests (see also forward declarations above)
    friend class ::Combine2GaussianFactorTest;
    friend class ::eliminateFrontalsGaussianFactorTest;
  };


  /** return A*x */
  Errors operator*(const FactorGraph<JacobianFactor>& fg, const VectorValues& x);

  /* In-place version e <- A*x that overwrites e. */
  void multiplyInPlace(const FactorGraph<JacobianFactor>& fg, const VectorValues& x, Errors& e);

  /* In-place version e <- A*x that takes an iterator. */
  void multiplyInPlace(const FactorGraph<JacobianFactor>& fg, const VectorValues& x, const Errors::iterator& e);

  /** x += alpha*A'*e */
  void transposeMultiplyAdd(const FactorGraph<JacobianFactor>& fg, double alpha, const Errors& e, VectorValues& x);

  /**
   * Calculate Gradient of A^(A*x-b) for a given config
   * @param x: VectorValues specifying where to calculate gradient
   * @return gradient, as a VectorValues as well
   */
  VectorValues gradient(const FactorGraph<JacobianFactor>& fg, const VectorValues& x);

  // matrix-vector operations
  void residual(const FactorGraph<JacobianFactor>& fg, const VectorValues &x, VectorValues &r);
  void multiply(const FactorGraph<JacobianFactor>& fg, const VectorValues &x, VectorValues &r);
  void transposeMultiply(const FactorGraph<JacobianFactor>& fg, const VectorValues &r, VectorValues &x);

}

