#include "oneflow/core/kernel/normalization_kernel.h"
#include "oneflow/core/common/data_type.h"

namespace oneflow {

namespace {

template<DeviceType device_type, typename T>
void Rsqrt(DeviceCtx* ctx, const int64_t n, const T* x, const float epsilon,
           T* y) {
  KernelUtil<device_type, T>::Copy(ctx, n, x, 1, y, 1);
  KernelUtil<device_type, T>::Rsqrt(ctx, n, y, epsilon);
}

template<DeviceType device_type, typename T>
void ScalarSub(DeviceCtx* ctx, const int64_t n, const T* x, const T* scalar_ptr,
               T* y) {
  KernelUtil<device_type, T>::Copy(ctx, n, x, 1, y, 1);
  KernelUtil<device_type, T>::Axpy(ctx, n, static_cast<T>(-1), scalar_ptr, 0, y,
                                   1);
}

}  // namespace

template<DeviceType device_type, typename T>
void NormalizationKernel<device_type, T>::InitModelBlobsWithRandomSeed(
    DeviceCtx* ctx, std::mt19937* random_seed_gen,
    std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  if (this->op_conf().normalization_conf().scale()) {
    InitializerConf gamma_init_conf;
    float gamma_init = this->op_conf().normalization_conf().gamma_init();
    gamma_init_conf.mutable_constant_conf()->set_value(gamma_init);
    KernelUtil<device_type, T>::Initialize(ctx, gamma_init_conf, 0,
                                           BnInOp2Blob("gamma"));
  }
  if (this->op_conf().normalization_conf().center()) {
    InitializerConf beta_init_conf;
    float beta_init = this->op_conf().normalization_conf().beta_init();
    beta_init_conf.mutable_constant_conf()->set_value(beta_init);
    KernelUtil<device_type, T>::Initialize(ctx, beta_init_conf, 0,
                                           BnInOp2Blob("beta"));
  }
  float mean_init = this->op_conf().normalization_conf().mean_init();
  InitializerConf moving_mean_init_conf;
  moving_mean_init_conf.mutable_constant_conf()->set_value(mean_init);
  KernelUtil<device_type, T>::Initialize(ctx, moving_mean_init_conf, 0,
                                         BnInOp2Blob("moving_mean"));
  float variance_init = this->op_conf().normalization_conf().variance_init();
  InitializerConf moving_variance_init_conf;
  moving_variance_init_conf.mutable_constant_conf()->set_value(variance_init);
  KernelUtil<device_type, T>::Initialize(ctx, moving_variance_init_conf, 0,
                                         BnInOp2Blob("moving_variance"));
}

template<DeviceType device_type, typename T>
void NormalizationKernel<device_type, T>::InitModelBlobsWithDir(
    DeviceCtx* ctx, int32_t part_id, int32_t part_num,
    const std::string& model_load_dir,
    std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  const auto& normalization_conf = this->op_conf().normalization_conf();
  if (normalization_conf.scale()) {
    Blob* gamma_blob = BnInOp2Blob("gamma");
    KernelUtil<device_type, T>::Initialize(ctx, 0, part_num, model_load_dir,
                                           gamma_blob, "gamma", gamma_blob->shape().At(0), gamma_blob->shape().Count(1));
  }
  if (normalization_conf.center()) {
    Blob* beta_blob = BnInOp2Blob("beta");
    KernelUtil<device_type, T>::Initialize(ctx, 0, part_num, model_load_dir,
                                           beta_blob, "beta", beta_blob->shape().At(0), beta_blob->shape().Count(1));
  }
  Blob* mean_blob = BnInOp2Blob("moving_mean");
  KernelUtil<device_type, T>::Initialize(ctx, 0, part_num, model_load_dir,
                                        mean_blob, 
                                         "moving_mean", mean_blob->shape().At(0), mean_blob->shape().Count(1));
  Blob* variance_blob = BnInOp2Blob("moving_variance");
  KernelUtil<device_type, T>::Initialize(ctx, 0, part_num, model_load_dir,
                                        variance_blob, 
                                         "moving_variance", variance_blob->shape().At(0), variance_blob->shape().Count(1));
}

template<DeviceType device_type, typename T>
void NormalizationKernel<device_type, T>::ForwardDataContent(
    const KernelCtx& ctx,
    std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  const auto& normalization_conf = this->op_conf().normalization_conf();
  const Blob* mean_blob = nullptr;
  const Blob* variance_blob = nullptr;
  const Blob*  
  Blob* in_blob = BnInOp2Blob("in");
  Blob* trans_in_blob = BnInOp2Blob("trans_in");
  Blob* trans_out_blob = BnInOp2Blob("trans_out");
  Blob* out_blob = BnInOp2Blob("out");
  int32_t num_axes = in_blob->shape().NumAxes();
  int32_t axis = normalization_conf.axis();
  bool need_transpose = axis + 1 < num_axes;
  PbRf<int32_t> perm_from;
  PbRf<int32_t> perm_to;
  if (need_transpose) {
    for (int32_t i = 0; i < num_axes; ++i) {
      perm_from.Add(i < axis ? i : i + 1);
      perm_to.Add(i > axis ? i - 1 : i);
    }
    perm_from.Set(num_axes - 1, axis);
    perm_to.Set(axis, num_axes - 1);
    in_blob->Transpose(ctx.device_type, trans_in_blob, perm_from);
  }
  if (Global<JobDesc>::Get()->IsTrain()) {
    CalcMeanAndVariance(ctx, BnInOp2Blob);
    UpdateMovingMeanAndMovingVariance(ctx, BnInOp2Blob);
    mean_blob = BnInOp2Blob("new_mean");
    variance_blob = BnInOp2Blob("new_variance");
  } else {
    mean_blob = BnInOp2Blob("moving_mean");
    variance_blob = BnInOp2Blob("moving_variance");
  }
  Normalize(ctx, BnInOp2Blob, mean_blob, variance_blob);
  if (need_transpose) {
    trans_out_blob->Transpose(ctx.device_type, out_blob, perm_to);
  }
}

template<DeviceType device_typ?, typename T>
void NormalizationKernel<device_type, T>::BackwardDataContent(
    const KernelCtx& ctx,
    std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  const auto& normalization_conf = this->op_conf().normalization_conf();
  const Blob* outputs_diff = BnInOp2Blob("outputs_diff");
  if (normalization_conf.center()) {
    Blob* beta_diff_blob = BnInOp2Blob("beta_diff");
    Blob* tmp_storage_blob = BnInOp2Blob("tmp_storage_for_sum");
    KernelUtil<device_type, T>::Sum(
        ctx.device_ctx, outputs_diff->shape().elem_cnt(),
        outputs_diff->dptr<T>(), beta_diff_blob->mut_dptr<T>(),
        tmp_storage_blob->mut_dptr<T>(), tmp_storage_blob->shape().elem_cnt());
  }
  Blob* inv_var_blob = BnInOp2Blob("inv_var");
  if (normalization_conf.scale()) {
    Blob* gamma_diff_blob = BnInOp2Blob("gamma_diff");
    const Blob* normalized_inputs_blob = BnInOp2Blob("normalized_inputs");
    KernelUtil<device_type, T>::Dot(
        ctx.device_ctx, outputs_diff->shape().elem_cnt(),
        outputs_diff->dptr<T>(), 1, normalized_inputs_blob->dptr<T>(), 1,
        gamma_diff_blob->mut_dptr<T>());
    KernelUtil<device_type, T>::Scal(
        ctx.device_ctx, inv_var_blob->shape().elem_cnt(),
        BnInOp2Blob("gamma")->dptr<T>(), inv_var_blob->mut_dptr<T>(), 1);
  }

  Blob* inputs_diff_blob = BnInOp2Blob("inputs_diff");
  if (inputs_diff_blob != nullptr) {
    KernelUtil<device_type, T>::Copy(
        ctx.device_ctx, inputs_diff_blob->shape().elem_cnt(),
        outputs_diff->dptr<T>(), 1, inputs_diff_blob->mut_dptr<T>(), 1);
    KernelUtil<device_type, T>::Scal(
        ctx.device_ctx, inputs_diff_blob->shape().elem_cnt(),
        inv_var_blob->dptr<T>(), inputs_diff_blob->mut_dptr<T>(), 1);
  }
}

template<DeviceType device_type, typename T>
void NormalizationKernel<device_type, T>::Normalize(
    const KernelCtx& ctx,
    const std::function<Blob*(const std::string&)>& BnInOp2Blob,
    const Blob* mean_blob, const Blob* variance_blob) const {
  const auto& normalization_conf = this->op_conf().normalization_conf();
  Blob* inv_var_blob = BnInOp2Blob("inv_var");
  Rsqrt<device_type, T>(ctx.device_ctx, 1, variance_blob->dptr<T>(),
                        normalization_conf.epsilon(),
                        inv_var_blob->mut_dptr<T>());
  const Blob* inputs_blob = BnInOp2Blob("inputs");
  bool scale = normalization_conf.scale();
  bool center = normalization_conf.center();
  Blob* normalized_blob =
      BnInOp2Blob((scale || center) ? "normalized_inputs" : "outputs");
  ScalarSub<device_type, T>(ctx.device_ctx, inputs_blob->shape().elem_cnt(),
                            inputs_blob->dptr<T>(), mean_blob->dptr<T>(),
                            normalized_blob->mut_dptr<T>());
  KernelUtil<device_type, T>::Scal(
      ctx.device_ctx, normalized_blob->shape().elem_cnt(),
      inv_var_blob->dptr<T>(), normalized_blob->mut_dptr<T>(), 1);
  Blob* outputs_blob = BnInOp2Blob("outputs");
  if (scale || center) {
    KernelUtil<device_type, T>::Copy(
        ctx.device_ctx, outputs_blob->shape().elem_cnt(),
        normalized_blob->dptr<T>(), 1, outputs_blob->mut_dptr<T>(), 1);
  }
  if (scale) {
    const Blob* gamma_blob = BnInOp2Blob("gamma");
    KernelUtil<device_type, T>::Scal(
        ctx.device_ctx, outputs_blob->shape().elem_cnt(), gamma_blob->dptr<T>(),
        outputs_blob->mut_dptr<T>(), 1);
  }

  if (center) {
    const Blob* beta_blob = BnInOp2Blob("beta");
    KernelUtil<device_type, T>::Axpy(
        ctx.device_ctx, outputs_blob->shape().elem_cnt(), static_cast<T>(1),
        beta_blob->dptr<T>(), 0, outputs_blob->mut_dptr<T>(), 1);
  }
}

template<DeviceType device_type, typename T>
void NormalizationKernel<device_type, T>::CalcMeanAndVariance(
    const KernelCtx& ctx,
    const std::function<Blob*(const std::string&)>& BnInOp2Blob) const {
  Blob* mean_blob = BnInOp2Blob("new_mean");
  const Blob* inputs_blob = BnInOp2Blob("inputs");
  Blob* tmp_storage_blob = BnInOp2Blob("tmp_storage_for_sum");
  const int32_t norm_num = mean_blob->shape().At(0);
  FOR_RANGE(int32_t, i, 0, norm_num) {
    KernelUtil<device_type, T>::Sum(
      ctx.device_ctx, inputs_blob->shape().elem_cnt(), inputs_blob->dptr<T>(),
      mean_blob->mut_dptr<T>(), tmp_storage_blob->mut_dptr<T>(),
      tmp_storage_blob->shape().elem_cnt());
  }
  const T inv_elem_cnt =
      this->kernel_conf().normalization_conf().inv_norm_elem_cnt();
  KernelUtil<device_type, T>::Scal(ctx.device_ctx,
                                   mean_blob->shape().elem_cnt(), inv_elem_cnt,
                                   mean_blob->mut_dptr<T>(), 1);

  //  It's safe to use `outputs' as tmp blob
  Blob* tmp_blob = BnInOp2Blob("outputs");
  ScalarSub<device_type, T>(ctx.device_ctx, inputs_blob->shape().elem_cnt(),
                            inputs_blob->dptr<T>(), mean_blob->dptr<T>(),
                            tmp_blob->mut_dptr<T>());
  KernelUtil<device_type, T>::Mul(ctx.device_ctx, tmp_blob->shape().elem_cnt(),
                                  tmp_blob->dptr<T>(), tmp_blob->dptr<T>(),
                                  tmp_blob->mut_dptr<T>());
  Blob* variance_blob = BnInOp2Blob("new_variance");
  KernelUtil<device_type, T>::Sum(
      ctx.device_ctx, tmp_blob->shape().elem_cnt(), tmp_blob->dptr<T>(),
      variance_blob->mut_dptr<T>(), tmp_storage_blob->mut_dptr<T>(),
      tmp_storage_blob->shape().elem_cnt());
  KernelUtil<device_type, T>::Scal(
      ctx.device_ctx, variance_blob->shape().elem_cnt(), inv_elem_cnt,
      variance_blob->mut_dptr<T>(), 1);
}

template<DeviceType device_type, typename T>
void NormalizationKernel<device_type, T>::UpdateMovingMeanAndMovingVariance(
    const KernelCtx& ctx,
    const std::function<Blob*(const std::string&)>& BnInOp2Blob) const {
  auto tpl = reinterpret_cast<
      std::tuple<int64_t, std::function<const Blob*(const std::string&)>>*>(
      ctx.other);
  int64_t piece_id = std::get<0>(*tpl);
  std::function<const Blob*(const std::string&)> lbn2preblob =
      std::get<1>(*tpl);
  const Blob* mean_blob = BnInOp2Blob("new_mean");
  const Blob* variance_blob = BnInOp2Blob("new_variance");
  Blob* moving_mean_blob = BnInOp2Blob("moving_mean");
  Blob* moving_variance_blob = BnInOp2Blob("moving_variance");
  if (piece_id == 0) {
    Memcpy<device_type>(ctx.device_ctx, moving_mean_blob->mut_dptr<T>(),
                        mean_blob->dptr<T>(),
                        mean_blob->shape().elem_cnt() * sizeof(T));
    Memcpy<device_type>(ctx.device_ctx, moving_variance_blob->mut_dptr<T>(),
                        variance_blob->dptr<T>(),
                        variance_blob->shape().elem_cnt() * sizeof(T));
    return;
  }
  const std::string& mean_lbn = this->Lbn4BnInOp("moving_mean");
  const Blob* pre_moving_mean_blob = lbn2preblob(mean_lbn);
  if (pre_moving_mean_blob != moving_mean_blob) {
    moving_mean_blob->CopyDataContentFrom(ctx.device_ctx, pre_moving_mean_blob);
  }
  const std::string& variance_lbn = this->Lbn4BnInOp("moving_variance");
  const Blob* pre_moving_variance_blob = lbn2preblob(variance_lbn);
  if (pre_moving_variance_blob != moving_variance_blob) {
    moving_variance_blob->CopyDataContentFrom(ctx.device_ctx,
                                              pre_moving_variance_blob);
  }
  const T momentum = this->op_conf().normalization_conf().momentum();
  const T one_minus_momentum = 1 - momentum;
  KernelUtil<device_type, T>::Scal(
      ctx.device_ctx, moving_mean_blob->shape().elem_cnt(), momentum,
      moving_mean_blob->mut_dptr<T>(), 1);
  KernelUtil<device_type, T>::Axpy(
      ctx.device_ctx, mean_blob->shape().elem_cnt(), one_minus_momentum,
      mean_blob->dptr<T>(), 1, moving_mean_blob->mut_dptr<T>(), 1);
  KernelUtil<device_type, T>::Scal(
      ctx.device_ctx, moving_variance_blob->shape().elem_cnt(), momentum,
      moving_variance_blob->mut_dptr<T>(), 1);
  KernelUtil<device_type, T>::Axpy(
      ctx.device_ctx, variance_blob->shape().elem_cnt(), one_minus_momentum,
      variance_blob->dptr<T>(), 1, moving_variance_blob->mut_dptr<T>(), 1);
}

ADD_DEFAULT_KERNEL_CREATOR(OperatorConf::kNormalizationConf,
                           NormalizationKernel, FLOATING_DATA_TYPE_SEQ);

}  // namespace oneflow
