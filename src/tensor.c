/// \file tensor.c
/// \brief Tensor structure, using column-major storage convention (first x, then y, z, ...)

#include "tensor.h"
#include "linalg.h"
#include "util.h"
#include "profiler.h"
#include <stdlib.h>
#include <memory.h>


//________________________________________________________________________________________________________________________
///
/// \brief Convert tensor index to data offset
///
static inline size_t IndexToOffset(const int ndim, const size_t *restrict dim, const size_t *restrict index)
{
	int i;
	size_t offset = 0;
	size_t dimfac = 1;
	for (i = 0; i < ndim; i++)
	{
		offset += dimfac*index[i];
		dimfac *= dim[i];
	}

	return offset;
}

//________________________________________________________________________________________________________________________
///
/// \brief Compute the lexicographically next tensor index
///
static inline void NextIndex(const int ndim, const size_t *restrict dim, size_t *restrict index)
{
	int i;
	for (i = 0; i < ndim; i++)
	{
		index[i]++;
		if (index[i] < dim[i])
		{
			return;
		}
		else
		{
			index[i] = 0;
		}
	}
}


//________________________________________________________________________________________________________________________
///
/// \brief Allocate memory for tensor
///
void AllocateTensor(const int ndim, const size_t *restrict dim, tensor_t *restrict t)
{
	assert(ndim >= 0);
	t->ndim = ndim;

	StartProfilingBlock(&std_profiler, PROFILE_ALLOCATE_TENSOR);

	if (ndim > 0)
	{
		t->dim = (size_t *)algn_malloc(ndim * sizeof(size_t));
		memcpy(t->dim, dim, ndim * sizeof(size_t));

		#ifdef _DEBUG
		t->dnames = (string_t *)algn_calloc(ndim, sizeof(string_t));
		#endif

		const size_t nelem = NumTensorElements(t);
		// dimensions must be strictly positive
		assert(nelem > 0);
		t->data = (double complex *)algn_calloc(nelem, sizeof(double complex));
		assert(t->data != NULL);
	}
	else    // ndim == 0
	{
		// algn_malloc(0) not guaranteed to return NULL
		t->dim = NULL;

		#ifdef _DEBUG
		t->dnames = NULL;
		#endif

		// allocate memory for a single number
		t->data = (double complex *)algn_calloc(1, sizeof(double complex));
		assert(t->data != NULL);
	}

	EndProfilingBlock(&std_profiler, PROFILE_ALLOCATE_TENSOR);
}


//________________________________________________________________________________________________________________________
///
/// \brief Delete tensor (free memory)
///
void DeleteTensor(tensor_t *restrict t)
{
	if (t->ndim > 0)
	{
		#ifdef _DEBUG
		algn_free(t->dnames);
		#endif

		algn_free(t->dim);
	}
	t->ndim = 0;

	algn_free(t->data);
	t->data = NULL;
}


//________________________________________________________________________________________________________________________
///
/// \brief Move tensor data (without allocating new memory)
///
void MoveTensorData(tensor_t *restrict src, tensor_t *restrict dst)
{
	dst->ndim = src->ndim;

	// move dimension pointers
	dst->dim = src->dim;
	src->dim = NULL;

	// move data pointers
	dst->data = src->data;
	src->data = NULL;

	#ifdef _DEBUG
	dst->dnames = src->dnames;
	src->dnames = NULL;
	#endif
}


//________________________________________________________________________________________________________________________
///
/// \brief Copy a tensor, allocating memory for the copy
///
void CopyTensor(const tensor_t *restrict src, tensor_t *restrict dst)
{
	AllocateTensor(src->ndim, src->dim, dst);

	const size_t nelem = NumTensorElements(src);

	assume_algned(src->data);
	assume_algned(dst->data);
	memcpy(dst->data, src->data, nelem*sizeof(double complex));

	#ifdef _DEBUG
	int i;
	for (i = 0; i < src->ndim; i++) {
		memcpy(dst->dnames[i].cstr, src->dnames[i].cstr, sizeof(string_t));
	}
	#endif
}


//________________________________________________________________________________________________________________________
///
/// \brief Set the tensor to the identity operator; all dimensions of the tensor must agree
///
void IdentityTensor(tensor_t *restrict t)
{
	int i;
	assert(t->ndim >= 1);
	const size_t n = t->dim[0];

	// geometric sum: stride = 1 + n + n*n + ...
	size_t stride = 1;
	size_t dp = 1;
	for (i = 1; i < t->ndim; i++)
	{
		assert(t->dim[i] == n);
		dp *= n;
		stride += dp;
	}
	assert(NumTensorElements(t) == n*dp);

	memset(t->data, 0, n*dp * sizeof(double complex));
	size_t j;
	for (j = 0; j < n; j++)
	{
		t->data[j*stride] = 1;
	}
}


//________________________________________________________________________________________________________________________
///
/// \brief Reshape dimensions, i.e., interpret as tensor of different dimension with same number of elements
///
void ReshapeTensor(const int ndim, const size_t *restrict dim, tensor_t *restrict t)
{
	// consistency check: number of elements must not change
	assert(IntProduct(dim, ndim) == NumTensorElements(t));

	// new dimensions
	t->ndim = ndim;
	algn_free(t->dim);
	t->dim = (size_t *)algn_malloc(ndim * sizeof(size_t));
	memcpy(t->dim, dim, ndim * sizeof(size_t));

	#ifdef _DEBUG
	algn_free(t->dnames);
	t->dnames = (string_t *)algn_calloc(ndim, sizeof(string_t));
	#endif
}


//________________________________________________________________________________________________________________________
///
/// \brief Pointwise conjugate
///
void ConjugateTensor(tensor_t *restrict t)
{
	const size_t nelem = NumTensorElements(t);
	size_t i;
	for (i = 0; i < nelem; i++)
	{
		t->data[i] = conj(t->data[i]);
	}
}


//________________________________________________________________________________________________________________________
///
/// \brief Generalized transpose of tensor 't' such that the k-th level in 't'
/// is the perm[k]-th level in the result 'r'
///
/// Memory will be allocated for 'r'.
///
void TransposeTensor(const int *restrict perm, const tensor_t *restrict t, tensor_t *restrict r)
{
	int i;

	StartProfilingBlock(&std_profiler, PROFILE_TRANSPOSE_TENSOR);

	// dimensions of new tensor 'r'
	size_t *rdim = (size_t *)algn_malloc(t->ndim * sizeof(size_t));
	for (i = 0; i < t->ndim; i++)
	{
		assert(0 <= perm[i] && perm[i] < t->ndim);
		rdim[perm[i]] = t->dim[i];
	}
	// create new tensor 'r'
	AllocateTensor(t->ndim, rdim, r);
	algn_free(rdim);

	// stride (offset between successive elements) in new tensor 'r' corresponding to original first dimension
	const size_t stride = IntProduct(r->dim, perm[0]);

	const size_t nelem = NumTensorElements(t);

	size_t *index_t = (size_t *)algn_calloc(t->ndim,  sizeof(size_t));
	size_t *index_r = (size_t *)algn_malloc(t->ndim * sizeof(size_t));

	size_t ot;
	for (ot = 0; ot < nelem; ot += t->dim[0])
	{
		// map index of tensor 't' to index of tensor 'r'
		for (i = 0; i < t->ndim; i++) {
			index_r[perm[i]] = index_t[i];
		}
		// convert back to offset of tensor 'r'
		const size_t or = IndexToOffset(r->ndim, r->dim, index_r);

		// main copy loop
		const size_t n = t->dim[0];
		assume_algned(t->data);
		assume_algned(r->data);
		size_t j;
		#pragma ivdep
		for (j = 0; j < n; j++)
		{
			r->data[or + j*stride] = t->data[ot + j];
		}

		// advance index of tensor 't' by t->dim[0] elements
		NextIndex(t->ndim - 1, t->dim + 1, index_t + 1);
	}

	// clean up
	algn_free(index_r);
	algn_free(index_t);

	#ifdef _DEBUG
	for (i = 0; i < t->ndim; i++) {
		memcpy(r->dnames[perm[i]].cstr, t->dnames[i].cstr, sizeof(string_t));
	}
	#endif

	EndProfilingBlock(&std_profiler, PROFILE_TRANSPOSE_TENSOR);
}


//________________________________________________________________________________________________________________________
///
/// \brief Generalized conjugate transpose of tensor 't' such that the k-th level in 't'
/// is the perm[k]-th level in the result 'r'
///
/// Memory will be allocated for 'r'.
///
void ConjugateTransposeTensor(const int *restrict perm, const tensor_t *restrict t, tensor_t *restrict r)
{
	TransposeTensor(perm, t, r);
	ConjugateTensor(r);
}


//________________________________________________________________________________________________________________________
///
/// \brief Assemble the sub-tensor with dimensions 'sdim' by taking elements indexed by 'idx' along each dimension
/// from the original tensor 't'; the number of dimensions remains the same as in 't'
///
void SubTensor(const tensor_t *restrict t, const size_t *restrict sdim, const size_t *restrict *idx, tensor_t *restrict s)
{
	// create new tensor 's'; number of dimensions agrees with 't'
	AllocateTensor(t->ndim, sdim, s);
	if (t->ndim == 0)
	{
		s->data[0] = t->data[0];
		return;
	}

	const size_t nelem = NumTensorElements(s);

	int i;

	size_t *index_t = (size_t *)algn_calloc(t->ndim, sizeof(size_t));
	size_t *index_s = (size_t *)algn_calloc(s->ndim, sizeof(size_t));

	// map first index of tensor 's' to index of tensor 't', except for first dimension (will be handled in copy loop)
	for (i = 1; i < t->ndim; i++)
	{
		assert(t->dim[i] > 0 && s->dim[i] > 0);
		index_t[i] = idx[i][0];
	}
	// convert back to offset of tensor 't'
	size_t ot = IndexToOffset(t->ndim, t->dim, index_t);

	size_t os;
	for (os = 0; os < nelem; os += s->dim[0])
	{
		// main copy loop along first dimension
		// (treating first dimension separately to avoid calling index conversion function for each single element)
		const size_t n = s->dim[0];
		assume_algned(t->data);
		assume_algned(s->data);
		size_t j;
		#pragma ivdep
		for (j = 0; j < n; j++)
		{
			s->data[os + j] = t->data[ot + idx[0][j]];
		}

		// advance index of tensor 's' by s->dim[0] elements
		NextIndex(s->ndim - 1, s->dim + 1, index_s + 1);
		// map index of tensor 's' to index of tensor 't', except for first dimension (will be handled in copy loop)
		for (i = 1; i < t->ndim; i++) {
			index_t[i] = idx[i][index_s[i]];
		}
		// convert back to offset of tensor 't'
		ot = IndexToOffset(t->ndim, t->dim, index_t);
	}

	#ifdef _DEBUG
	for (i = 0; i < t->ndim; i++) {
		memcpy(s->dnames[i].cstr, t->dnames[i].cstr, sizeof(string_t));
	}
	#endif

	// clean up
	algn_free(index_s);
	algn_free(index_t);
}


//________________________________________________________________________________________________________________________
///
/// \brief Scale tensor t by alpha
///
void ScaleTensor(const double alpha, tensor_t *restrict t)
{
	cblas_zdscal(NumTensorElements(t), alpha, t->data, 1);
}


//________________________________________________________________________________________________________________________
///
/// \brief Scalar multiply and add two tensors: t = alpha*s + t; dimensions of s and t must agree
///
void ScalarMultiplyAddTensor(const double complex alpha, const tensor_t *restrict s, tensor_t *restrict t)
{
	const size_t nelem = NumTensorElements(s);

	assert(s->ndim == t->ndim);
	assert(nelem == NumTensorElements(t));

	cblas_zaxpy((int)nelem, &alpha, s->data, 1, t->data, 1);
}


//________________________________________________________________________________________________________________________
///
/// \brief Multiply last 'ndim_mult' dimensions in 's' by first 'ndim_mult' dimension in 't', and store result in 'r'
///
/// Memory will be allocated for 'r'.
///
void MultiplyTensor(const tensor_t *restrict s, const tensor_t *restrict t, const int ndim_mult, tensor_t *restrict r)
{
	int i;

	StartProfilingBlock(&std_profiler, PROFILE_MULTIPLY_TENSOR);

	assert(ndim_mult >= 1);
	assert(s->ndim >= ndim_mult && t->ndim >= ndim_mult);
	for (i = 0; i < ndim_mult; i++)
	{
		assert(s->dim[s->ndim - ndim_mult + i] == t->dim[i]);
	}

	// dimensions of new tensor 'r'
	size_t *rdim = (size_t *)algn_malloc((s->ndim + t->ndim - 2*ndim_mult) * sizeof(size_t));
	for (i = 0; i < s->ndim - ndim_mult; i++)
	{
		rdim[i] = s->dim[i];
	}
	for (i = ndim_mult; i < t->ndim; i++)
	{
		rdim[s->ndim + i - 2*ndim_mult] = t->dim[i];
	}
	// create new tensor 'r'
	AllocateTensor(s->ndim + t->ndim - 2*ndim_mult, rdim, r);
	algn_free(rdim);
	// copy dimension names
	#ifdef _DEBUG
	for (i = 0; i < s->ndim - ndim_mult; i++)
	{
		memcpy(r->dnames[i].cstr, s->dnames[i].cstr, sizeof(string_t));
	}
	for (i = ndim_mult; i < t->ndim; i++)
	{
		memcpy(r->dnames[s->ndim + i - 2*ndim_mult].cstr, t->dnames[i].cstr, sizeof(string_t));
	}
	#endif

	const size_t nelemS = NumTensorElements(s);
	const size_t nelemT = NumTensorElements(t);

	// leading dimension of 's' as a matrix
	const size_t lds = nelemS / IntProduct(&s->dim[s->ndim - ndim_mult], ndim_mult);
	assert(lds > 0);

	// leading dimension of 't' as a matrix
	const size_t ldt = IntProduct(t->dim, ndim_mult);
	assert(ldt > 0);
	// trailing dimension of 't' as a matrix
	const size_t tdt = nelemT / ldt;

	if (lds == 1)
	{
		if (tdt == 1)
		{
			// inner product of two vectors
			assert(nelemS == nelemT);
			assert(NumTensorElements(r) == 1);
			cblas_zdotu_sub((int)nelemS, s->data, 1, t->data, 1, &r->data[0]);
		}
		else    // tdt > 1
		{
			// multiply vector 's' from left, i.e., (t^T * s)^T
			const double complex one  = 1;
			const double complex zero = 0;
			cblas_zgemv(CblasColMajor, CblasTrans, (int)ldt, (int)tdt, &one, t->data, (int)ldt, s->data, 1, &zero, r->data, 1);
		}
	}
	else    // lds > 1
	{
		if (tdt == 1)
		{
			// matrix-vector multiplication
			const double complex one  = 1;
			const double complex zero = 0;
			cblas_zgemv(CblasColMajor, CblasNoTrans, (int)lds, (int)ldt, &one, s->data, (int)lds, t->data, 1, &zero, r->data, 1);
		}
		else    // tdt > 1
		{
			// matrix-matrix multiplication
			const double complex one  = 1;
			const double complex zero = 0;
			cblas_zgemm(CblasColMajor, CblasNoTrans, CblasNoTrans, (int)lds, (int)tdt, (int)ldt, &one, s->data, (int)lds, t->data, (int)ldt, &zero, r->data, (int)lds);
		}
	}

	EndProfilingBlock(&std_profiler, PROFILE_MULTIPLY_TENSOR);
}


//________________________________________________________________________________________________________________________
///
/// \brief Compute the Kronecker product of two tensors; the tensors must have the same number of levels
///
void TensorKroneckerProduct(const tensor_t *restrict s, const tensor_t *restrict t, tensor_t *restrict r)
{
	int i;

	// tensors must have the same number of levels
	assert(s->ndim == t->ndim);

	// dimensions of temporary tensor 'u'
	size_t *udim = (size_t *)algn_malloc((s->ndim + t->ndim) * sizeof(size_t));
	for (i = 0; i < s->ndim; i++)
	{
		udim[i] = s->dim[i];
	}
	for (i = 0; i < t->ndim; i++)
	{
		udim[s->ndim + i] = t->dim[i];
	}
	// create temporary tensor 'u'
	tensor_t u;
	AllocateTensor(s->ndim + t->ndim, udim, &u);
	algn_free(udim);

	const size_t m = NumTensorElements(s);
	const size_t n = NumTensorElements(t);
	assert(NumTensorElements(&u) == m*n);

	// outer product; u.data must have been initialized with zeros
	const double complex one = 1;
	cblas_zgeru(CblasColMajor, (int)m, (int)n, &one, s->data, 1, t->data, 1, u.data, (int)m);

	// reorder levels of 'u' and store result in 'r'
	int *perm = (int *)algn_malloc((s->ndim + t->ndim) * sizeof(int));
	for (i = 0; i < s->ndim; i++)
	{
		perm[i] = 2*i;
	}
	for (i = 0; i < t->ndim; i++)
	{
		perm[s->ndim + i] = 2*i + 1;
	}
	TransposeTensor(perm, &u, r);
	algn_free(perm);

	DeleteTensor(&u);

	// final dimensions of tensor 'r': pointwise products of dimensions of 's' and 't';
	// cannot automatically determine dimension names
	size_t *rdim = (size_t *)algn_malloc(s->ndim * sizeof(size_t));
	for (i = 0; i < s->ndim; i++)
	{
		rdim[i] = s->dim[i] * t->dim[i];
		assert(rdim[i] == r->dim[2*i]*r->dim[2*i+1]);
	}
	ReshapeTensor(s->ndim, rdim, r);
	algn_free(rdim);
}


//________________________________________________________________________________________________________________________
///
/// \brief Compute the 'trace' of a tensor (generalization of the matrix trace); all dimensions of the tensor must agree
///
double complex TensorTrace(const tensor_t *restrict t)
{
	int i;
	assert(t->ndim >= 1);
	const size_t n = t->dim[0];

	// geometric sum: stride = 1 + n + n*n + ...
	size_t stride = 1;
	size_t dp = 1;
	for (i = 1; i < t->ndim; i++)
	{
		assert(t->dim[i] == n);
		dp *= n;
		stride += dp;
	}

	size_t j;
	double complex tr = 0;
	for (j = 0; j < n; j++)
	{
		tr += t->data[j*stride];
	}

	return tr;
}
