#include "tensorflow/core/framework/op.h"
#include "tensorflow/core/framework/op_kernel.h"
#include "tensorflow/core/framework/shape_inference.h"
#include <iostream>

using namespace tensorflow;
using namespace std;

#ifdef HIGH_PREC
typedef double VALUETYPE;
#else
typedef float  VALUETYPE;
#endif

#ifdef HIGH_PREC
REGISTER_OP("ProdForce")
.Input("net_deriv: double")
.Input("in_deriv: double")
.Input("nlist: int32")
.Input("axis: int32")
.Input("natoms: int32")
.Attr("n_a_sel: int")
.Attr("n_r_sel: int")
.Attr("num_threads: int = 1")
.Output("force: double");
#else
REGISTER_OP("ProdForce")
.Input("net_deriv: float")
.Input("in_deriv: float")
.Input("nlist: int32")
.Input("axis: int32")
.Input("natoms: int32")
.Attr("n_a_sel: int")
.Attr("n_r_sel: int")
.Attr("num_threads: int = 1")
.Output("force: float");
#endif

using namespace tensorflow;

class ProdForceOp : public OpKernel {
 public:
  explicit ProdForceOp(OpKernelConstruction* context) : OpKernel(context) {
    OP_REQUIRES_OK(context, context->GetAttr("n_a_sel", &n_a_sel));
    OP_REQUIRES_OK(context, context->GetAttr("n_r_sel", &n_r_sel));
    OP_REQUIRES_OK(context, context->GetAttr("num_threads", &num_threads));
    n_a_shift = n_a_sel * 4;
  }

  void Compute(OpKernelContext* context) override {
    // Grab the input tensor
    const Tensor& net_deriv_tensor	= context->input(0);
    const Tensor& in_deriv_tensor	= context->input(1);
    const Tensor& nlist_tensor		= context->input(2);
    const Tensor& axis_tensor		= context->input(3);
    const Tensor& natoms_tensor		= context->input(4);

    // set size of the sample
    OP_REQUIRES (context, (net_deriv_tensor.shape().dims() == 2),	errors::InvalidArgument ("Dim of net deriv should be 2"));
    OP_REQUIRES (context, (in_deriv_tensor.shape().dims() == 2),	errors::InvalidArgument ("Dim of input deriv should be 2"));
    OP_REQUIRES (context, (nlist_tensor.shape().dims() == 2),		errors::InvalidArgument ("Dim of nlist should be 2"));
    OP_REQUIRES (context, (axis_tensor.shape().dims() == 2),		errors::InvalidArgument ("Dim of axis should be 2"));
    OP_REQUIRES (context, (natoms_tensor.shape().dims() == 1),		errors::InvalidArgument ("Dim of natoms should be 1"));

    OP_REQUIRES (context, (natoms_tensor.shape().dim_size(0) >= 3),	errors::InvalidArgument ("number of atoms should be larger than (or equal to) 3"));
    auto natoms	= natoms_tensor	.flat<int>();

    int nframes = net_deriv_tensor.shape().dim_size(0);
    int nloc = natoms(0);
    int nall = natoms(1);
    int ndescrpt = net_deriv_tensor.shape().dim_size(1) / nloc;
    int nnei = nlist_tensor.shape().dim_size(1) / nloc;

    // check the sizes
    OP_REQUIRES (context, (nframes == in_deriv_tensor.shape().dim_size(0)),	errors::InvalidArgument ("number of samples should match"));
    OP_REQUIRES (context, (nframes == nlist_tensor.shape().dim_size(0)),	errors::InvalidArgument ("number of samples should match"));
    OP_REQUIRES (context, (nframes == axis_tensor.shape().dim_size(0)),		errors::InvalidArgument ("number of samples should match"));

    OP_REQUIRES (context, (nloc * ndescrpt * 12 == in_deriv_tensor.shape().dim_size(1)), errors::InvalidArgument ("number of descriptors should match"));
    OP_REQUIRES (context, (nnei == n_a_sel + n_r_sel),				errors::InvalidArgument ("number of neighbors should match"));
    OP_REQUIRES (context, (nloc * 4 == axis_tensor.shape().dim_size(1)),	errors::InvalidArgument ("number of axis type+id should match 2+2"));

    // Create an output tensor
    TensorShape force_shape ;
    force_shape.AddDim (nframes);
    force_shape.AddDim (3 * nall);
    // cout << "forcesahpe " << force_shape.dim_size(0) << " " << force_shape.dim_size(1) << endl;
    Tensor* force_tensor = NULL;
    OP_REQUIRES_OK(context, context->allocate_output(0, force_shape, &force_tensor));
    
    // flat the tensors
    auto net_deriv = net_deriv_tensor.flat<VALUETYPE>();
    auto in_deriv = in_deriv_tensor.flat<VALUETYPE>();
    auto nlist = nlist_tensor.flat<int>();
    auto axis = axis_tensor.flat<int>();
    auto force = force_tensor->flat<VALUETYPE>();

    // loop over samples
    int net_iter = 0;
    int in_iter = 0;
    int force_iter = 0;
    int nlist_iter = 0;
    int axis_iter = 0;
    
#pragma omp parallel for num_threads (num_threads)
    for (int kk = 0; kk < nframes; ++kk){
      force_iter	= kk * nloc * 3;
      net_iter		= kk * nloc * ndescrpt;
      in_iter		= kk * nloc * ndescrpt * 12;
      nlist_iter	= kk * nloc * nnei;
      axis_iter		= kk * nloc * 4;

      for (int ii = 0; ii < nloc; ++ii){
	int i_idx = ii;
	force (force_iter + i_idx * 3 + 0) = 0;
	force (force_iter + i_idx * 3 + 1) = 0;
	force (force_iter + i_idx * 3 + 2) = 0;
      }

      // compute force of a frame
      for (int ii = 0; ii < nloc; ++ii){
	int i_idx = ii;
	
	// deriv wrt center atom
	for (int aa = 0; aa < ndescrpt; ++aa){
	  force (force_iter + i_idx * 3 + 0) -= net_deriv (net_iter + i_idx * ndescrpt + aa) * in_deriv (in_iter + i_idx * ndescrpt * 12 + aa * 12 + 0);
	  force (force_iter + i_idx * 3 + 1) -= net_deriv (net_iter + i_idx * ndescrpt + aa) * in_deriv (in_iter + i_idx * ndescrpt * 12 + aa * 12 + 1);
	  force (force_iter + i_idx * 3 + 2) -= net_deriv (net_iter + i_idx * ndescrpt + aa) * in_deriv (in_iter + i_idx * ndescrpt * 12 + aa * 12 + 2);
	}

	// set axes
	int axis0_type = axis (axis_iter + i_idx * 4 + 0);
	int axis1_type = axis (axis_iter + i_idx * 4 + 2);
	int axis_0  = axis (axis_iter + i_idx * 4 + 1);
	int axis_1  = axis (axis_iter + i_idx * 4 + 3);
	if (axis0_type == 1) axis_0 += n_a_sel;
	if (axis1_type == 1) axis_1 += n_a_sel;

	// deriv wrt neighbors
	for (int jj = 0; jj < nnei; ++jj){
	  int j_idx = nlist (nlist_iter + i_idx * nnei + jj);
	  if (j_idx > nloc) j_idx = j_idx % nloc;
	  if (j_idx < 0) continue;
	  if (jj == axis_0) {
	    for (int aa = 0; aa < ndescrpt; ++aa){
	      force (force_iter + j_idx * 3 + 0) -= net_deriv (net_iter + i_idx * ndescrpt + aa) * in_deriv (in_iter + i_idx * ndescrpt * 12 + aa * 12 + 3 + 0);
	      force (force_iter + j_idx * 3 + 1) -= net_deriv (net_iter + i_idx * ndescrpt + aa) * in_deriv (in_iter + i_idx * ndescrpt * 12 + aa * 12 + 3 + 1);
	      force (force_iter + j_idx * 3 + 2) -= net_deriv (net_iter + i_idx * ndescrpt + aa) * in_deriv (in_iter + i_idx * ndescrpt * 12 + aa * 12 + 3 + 2);
	    }
	  }
	  else if (jj == axis_1) {
	    for (int aa = 0; aa < ndescrpt; ++aa){
	      force (force_iter + j_idx * 3 + 0) -= net_deriv (net_iter + i_idx * ndescrpt + aa) * in_deriv (in_iter + i_idx * ndescrpt * 12 + aa * 12 + 6 + 0);
	      force (force_iter + j_idx * 3 + 1) -= net_deriv (net_iter + i_idx * ndescrpt + aa) * in_deriv (in_iter + i_idx * ndescrpt * 12 + aa * 12 + 6 + 1);
	      force (force_iter + j_idx * 3 + 2) -= net_deriv (net_iter + i_idx * ndescrpt + aa) * in_deriv (in_iter + i_idx * ndescrpt * 12 + aa * 12 + 6 + 2);
	    }
	  }
	  else {
	    int aa_start, aa_end;
	    make_descript_range (aa_start, aa_end, jj);
	    for (int aa = aa_start; aa < aa_end; ++aa) {
	      force (force_iter + j_idx * 3 + 0) -= net_deriv (net_iter + i_idx * ndescrpt + aa) * in_deriv (in_iter + i_idx * ndescrpt * 12 + aa * 12 + 9 + 0);
	      force (force_iter + j_idx * 3 + 1) -= net_deriv (net_iter + i_idx * ndescrpt + aa) * in_deriv (in_iter + i_idx * ndescrpt * 12 + aa * 12 + 9 + 1);
	      force (force_iter + j_idx * 3 + 2) -= net_deriv (net_iter + i_idx * ndescrpt + aa) * in_deriv (in_iter + i_idx * ndescrpt * 12 + aa * 12 + 9 + 2);
	    }
	  }
	}
      }
    }
  }
private:
  int n_r_sel, n_a_sel, n_a_shift, num_threads;
  inline void 
  make_descript_range (int & idx_start,
		       int & idx_end,
		       const int & nei_idx) {
    if (nei_idx < n_a_sel) {
      idx_start = nei_idx * 4;
      idx_end   = nei_idx * 4 + 4;
    }
    else {
      idx_start = n_a_shift + (nei_idx - n_a_sel);
      idx_end   = n_a_shift + (nei_idx - n_a_sel) + 1;
    }
  }
};

REGISTER_KERNEL_BUILDER(Name("ProdForce").Device(DEVICE_CPU), ProdForceOp);



