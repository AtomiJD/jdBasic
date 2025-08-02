You need the actual eigen distribution as subfolder:
jdBasic\plugins\numerics

├───eigen-3.4.0
│   ├───.gitlab
│   │   ├───issue_templates
│   │   └───merge_request_templates
│   ├───bench
│   │   ├───btl
│   │   │   ├───actions
│   │   │   ├───cmake
│   │   │   ├───data
│   │   │   ├───generic_bench
│   │   │   │   ├───init
│   │   │   │   ├───static
│   │   │   │   ├───timers
│   │   │   │   └───utils
│   │   │   └───libs
│   │   │       ├───BLAS
│   │   │       ├───blaze
│   │   │       ├───blitz
│   │   │       ├───eigen2
│   │   │       ├───eigen3
│   │   │       ├───gmm
│   │   │       ├───mtl4
│   │   │       ├───STL
│   │   │       ├───tensors
│   │   │       ├───tvmet
│   │   │       └───ublas
│   │   ├───perf_monitoring
│   │   │   └───resources
│   │   ├───spbench
│   │   └───tensors
│   ├───blas
│   │   ├───f2c
│   │   ├───fortran
│   │   └───testing
│   ├───ci
│   ├───cmake
│   ├───debug
│   │   ├───gdb
│   │   └───msvc
│   ├───demos
│   │   ├───mandelbrot
│   │   ├───mix_eigen_and_c
│   │   └───opengl
│   ├───doc
│   │   ├───examples
│   │   ├───snippets
│   │   └───special_examples
│   ├───Eigen
│   │   └───src
│   │       ├───Cholesky
│   │       ├───CholmodSupport
│   │       ├───Core
│   │       │   ├───arch
│   │       │   │   ├───AltiVec
│   │       │   │   ├───AVX
│   │       │   │   ├───AVX512
│   │       │   │   ├───CUDA
│   │       │   │   ├───Default
│   │       │   │   ├───GPU
│   │       │   │   ├───HIP
│   │       │   │   │   └───hcc
│   │       │   │   ├───MSA
│   │       │   │   ├───NEON
│   │       │   │   ├───SSE
│   │       │   │   ├───SVE
│   │       │   │   ├───SYCL
│   │       │   │   └───ZVector
│   │       │   ├───functors
│   │       │   ├───products
│   │       │   └───util
│   │       ├───Eigenvalues
│   │       ├───Geometry
│   │       │   └───arch
│   │       ├───Householder
│   │       ├───IterativeLinearSolvers
│   │       ├───Jacobi
│   │       ├───KLUSupport
│   │       ├───LU
│   │       │   └───arch
│   │       ├───MetisSupport
│   │       ├───misc
│   │       ├───OrderingMethods
│   │       ├───PardisoSupport
│   │       ├───PaStiXSupport
│   │       ├───plugins
│   │       ├───QR
│   │       ├───SparseCholesky
│   │       ├───SparseCore
│   │       ├───SparseLU
│   │       ├───SparseQR
│   │       ├───SPQRSupport
│   │       ├───StlSupport
│   │       ├───SuperLUSupport
│   │       ├───SVD
│   │       └───UmfPackSupport
│   ├───failtest
│   ├───lapack
│   ├───scripts
│   ├───test
│   └───unsupported
│       ├───bench
│       ├───doc
│       │   ├───examples
│       │   │   └───SYCL
│       │   └───snippets
│       ├───Eigen
│       │   ├───CXX11
│       │   │   └───src
│       │   │       ├───Tensor
│       │   │       ├───TensorSymmetry
│       │   │       │   └───util
│       │   │       ├───ThreadPool
│       │   │       └───util
│       │   └───src
│       │       ├───AutoDiff
│       │       ├───BVH
│       │       ├───Eigenvalues
│       │       ├───EulerAngles
│       │       ├───FFT
│       │       ├───IterativeSolvers
│       │       ├───KroneckerProduct
│       │       ├───LevenbergMarquardt
│       │       ├───MatrixFunctions
│       │       ├───MoreVectorization
│       │       ├───NonLinearOptimization
│       │       ├───NumericalDiff
│       │       ├───Polynomials
│       │       ├───Skyline
│       │       ├───SparseExtra
│       │       ├───SpecialFunctions
│       │       │   └───arch
│       │       │       ├───AVX
│       │       │       ├───AVX512
│       │       │       ├───GPU
│       │       │       └───NEON
│       │       └───Splines
│       └───test