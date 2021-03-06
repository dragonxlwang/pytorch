#ifndef THC_GENERIC_FILE
#define THC_GENERIC_FILE "generic/THCStorage.cpp"
#else

real* THCStorage_(data)(THCState *state, const THCStorage *self)
{
  return self->data<real>();
}

ptrdiff_t THCStorage_(size)(THCState *state, const THCStorage *self)
{
  return self->size;
}

int THCStorage_(elementSize)(THCState *state)
{
  return sizeof(real);
}

void THCStorage_(set)(THCState *state, THCStorage *self, ptrdiff_t index, real value)
{
  THArgCheck((index >= 0) && (index < self->size), 2, "index out of bounds");
  cudaStream_t stream = THCState_getCurrentStream(state);
  THCudaCheck(cudaMemcpyAsync(THCStorage_(data)(state, self) + index, &value, sizeof(real),
                              cudaMemcpyHostToDevice,
                              stream));
  THCudaCheck(cudaStreamSynchronize(stream));
}

real THCStorage_(get)(THCState *state, const THCStorage *self, ptrdiff_t index)
{
  THArgCheck((index >= 0) && (index < self->size), 2, "index out of bounds");
  real value;
  cudaStream_t stream = THCState_getCurrentStream(state);
  THCudaCheck(cudaMemcpyAsync(&value, THCStorage_(data)(state, self) + index, sizeof(real),
                              cudaMemcpyDeviceToHost, stream));
  THCudaCheck(cudaStreamSynchronize(stream));
  return value;
}

THCStorage* THCStorage_(new)(THCState *state)
{
  return THCStorage_(newWithSize)(state, 0);
}

THCStorage* THCStorage_(newWithSize)(THCState *state, ptrdiff_t size)
{
  return THCStorage_(newWithAllocator)(
    state, size,
    state->cudaDeviceAllocator,
    state->cudaDeviceAllocator->state);
}

THCStorage* THCStorage_(newWithAllocator)(THCState *state, ptrdiff_t size,
                                          THCDeviceAllocator* allocator,
                                          void* allocatorContext)
{
  THArgCheck(size >= 0, 2, "invalid size");
  int device;
  THCudaCheck(cudaGetDevice(&device));

  THCStorage *storage = (THCStorage*)THAlloc(sizeof(THCStorage));
  memset(storage, 0, sizeof(THCStorage));
  new (&storage->refcount) std::atomic<int>(1);
  storage->scalar_type = at::CTypeToScalarType<at::cuda::from_type<real>>::to();
  storage->flag = TH_STORAGE_REFCOUNTED | TH_STORAGE_RESIZABLE | TH_STORAGE_FREEMEM;
  storage->allocator = allocator;
  storage->allocatorContext = allocatorContext;
  storage->size = size;
  storage->device = device;

  if(size > 0)
  {
    // update heap *before* attempting malloc, to free space for the malloc
    cudaError_t err =
      (*allocator->malloc)(allocatorContext,
                           (void**)&(storage->data_ptr),
                           size * sizeof(real),
                           THCState_getCurrentStream(state));
    if(err != cudaSuccess){
      free(storage);
    }
    THCudaCheck(err);
  } else {
    storage->data_ptr = NULL;
  }
  return storage;
}

THCStorage* THCStorage_(newWithSize1)(THCState *state, real data0)
{
  THCStorage *self = THCStorage_(newWithSize)(state, 1);
  THCStorage_(set)(state, self, 0, data0);
  return self;
}

THCStorage* THCStorage_(newWithSize2)(THCState *state, real data0, real data1)
{
  THCStorage *self = THCStorage_(newWithSize)(state, 2);
  THCStorage_(set)(state, self, 0, data0);
  THCStorage_(set)(state, self, 1, data1);
  return self;
}

THCStorage* THCStorage_(newWithSize3)(THCState *state, real data0, real data1, real data2)
{
  THCStorage *self = THCStorage_(newWithSize)(state, 3);
  THCStorage_(set)(state, self, 0, data0);
  THCStorage_(set)(state, self, 1, data1);
  THCStorage_(set)(state, self, 2, data2);
  return self;
}

THCStorage* THCStorage_(newWithSize4)(THCState *state, real data0, real data1, real data2, real data3)
{
  THCStorage *self = THCStorage_(newWithSize)(state, 4);
  THCStorage_(set)(state, self, 0, data0);
  THCStorage_(set)(state, self, 1, data1);
  THCStorage_(set)(state, self, 2, data2);
  THCStorage_(set)(state, self, 3, data3);
  return self;
}

THCStorage* THCStorage_(newWithMapping)(THCState *state, const char *fileName, ptrdiff_t size, int isShared)
{
  THError("not available yet for THCStorage");
  return NULL;
}

THCStorage* THCStorage_(newWithData)(THCState *state, real *data, ptrdiff_t size)
{
  return THCStorage_(newWithDataAndAllocator)(state, data, size,
                                              state->cudaDeviceAllocator,
                                              state->cudaDeviceAllocator->state);
}

THCStorage* THCStorage_(newWithDataAndAllocator)(
  THCState *state, real *data, ptrdiff_t size,
  THCDeviceAllocator *allocator, void *allocatorContext) {
  THCStorage *storage = (THCStorage*)THAlloc(sizeof(THCStorage));
  memset(storage, 0, sizeof(THCStorage));
  storage->scalar_type = at::CTypeToScalarType<at::cuda::from_type<real>>::to();
  storage->data_ptr = data;
  storage->size = size;
  storage->refcount = 1;
  storage->flag = TH_STORAGE_REFCOUNTED | TH_STORAGE_RESIZABLE | TH_STORAGE_FREEMEM;
  storage->allocator = allocator;
  storage->allocatorContext = allocatorContext;
  int device;
  if (data) {
    struct cudaPointerAttributes attr;
    THCudaCheck(cudaPointerGetAttributes(&attr, data));
    device = attr.device;
  } else {
    THCudaCheck(cudaGetDevice(&device));
  }
  storage->device = device;
  return storage;
}

void THCStorage_(setFlag)(THCState *state, THCStorage *storage, const char flag)
{
  storage->flag |= flag;
}

void THCStorage_(clearFlag)(THCState *state, THCStorage *storage, const char flag)
{
  storage->flag &= ~flag;
}

void THCStorage_(retain)(THCState *state, THCStorage *self)
{
  if(self && (self->flag & TH_STORAGE_REFCOUNTED))
    self->refcount++;
}

int THCStorage_(retainIfLive)(THCState *state, THCStorage *storage)
{
  // TODO: Check if THC_STORAGE_REFCOUNTED?
  int refcount = storage->refcount.load();
  while (refcount > 0) {
    if (storage->refcount.compare_exchange_strong(refcount, refcount + 1)) {
      return 1;
    }
    refcount = storage->refcount.load();
  }
  return 0;
}

void THCStorage_(free)(THCState *state, THCStorage *self)
{
  if(!(self->flag & TH_STORAGE_REFCOUNTED))
    return;

  if (--self->refcount == 0)
  {
    if(self->flag & TH_STORAGE_FREEMEM) {
      THCudaCheck(
        (*self->allocator->free)(self->allocatorContext, THCStorage_(data)(state, self)));
    }
    if(self->flag & TH_STORAGE_VIEW) {
      THCStorage_(free)(state, self->view);
    }
    self->refcount.~atomic<int>();
    THFree(self);
  }
}
#endif
