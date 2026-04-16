# VulkEase Package TODO

This file tracks VulkEase functions that still need a Mobius package API.
It is focused on native/package-facing functions, not the script-side helper
constructors and enum tables already present in `vulkease.mob`.

Audit status:

- Checked against all public `VULKEASE_API ve*` exports in `vulkease.h`
- Header export count at audit time: `219`
- Functions already wrapped in the package: `12`
- Remaining functions tracked here: `207`
- Goal for this file: every public `ve*` export should appear either in
  `Already covered` or in one of the unchecked TODO sections below

Already covered:

- `veGetVersion`
- `veResultToString`
- `veCreateContext`
- `veDestroyContext`
- `veEnumeratePhysicalDevices`
- `veGetPhysicalDeviceInfo`
- `veCreateDevice`
- `veDestroyDevice`
- `veIsInstanceExtensionAvailable`
- `veDeviceWaitIdle`
- `veGetVulkanVersion`
- `veIsMeshShaderSupported`

## High Priority Missing APIs

These are the biggest gaps for making the package broadly usable.

### Device / Instance Queries

- [ ] `veIsDeviceExtensionAvailable`
- [ ] `veGetDeviceName`
- [ ] `veGetDriverVersion`
- [ ] `veGetVkInstance`
- [ ] `veGetVkPhysicalDevice`
- [ ] `veGetVkDevice`
- [ ] `veGetVkGraphicsQueue`
- [ ] `veGetVkComputeQueue`
- [ ] `veGetVkTransferQueue`
- [ ] `veGetVkCommandBufferFence`
- [ ] `veGetVkBufferFromAddress`
- [ ] `veGetVkImageFromTexture`
- [ ] `veGetVkImageViewFromTexture`
- [ ] `veGetVkSamplerFromIndex`
- [ ] `veGetVkSwapchain`
- [ ] `veGetVkSwapchainImage`
- [ ] `veGetVkSwapchainImageView`

### Message / Logging

- [ ] `veSetMessageCallback`
- [ ] `veSetMinMessageSeverity`

### Synchronization

- [ ] `veCreateFence`
- [ ] `veDestroyFence`
- [ ] `veWaitFence`
- [ ] `veGetFenceStatus`
- [ ] `veResetFence`

### Buffer API

- [ ] `veCreateBuffer`
- [ ] `veDestroyBuffer`
- [ ] `veMapBuffer`
- [ ] `veUnmapBuffer`
- [ ] `veUpdateBuffer`
- [ ] `veGetBufferSize`
- [ ] `veGetBufferUsage`
- [ ] `veCreateVertexBuffer`
- [ ] `veCreateIndexBuffer`
- [ ] `veCreateUniformBuffer`
- [ ] `veCreateStorageBuffer`
- [ ] `veCreateIndirectBuffer`
- [ ] `veCmdCopyBuffer`
- [ ] `veCopyBuffer`
- [ ] `veCmdFillBuffer`
- [ ] `veCmdUpdateBuffer`

### Texture API

- [ ] `veCreateTexture`
- [ ] `veDestroyTexture`
- [ ] `veGetTextureSize`
- [ ] `veGetTextureFormat`
- [ ] `veCreateTexture1D`
- [ ] `veCreateTexture2D`
- [ ] `veCreateTexture3D`
- [ ] `veCreateTexture2DArray`
- [ ] `veCreateTextureCube`
- [ ] `veCreateTexture2DMultisample`
- [ ] `veLoadTexture`
- [ ] `veLoadHDRTexture`
- [ ] `veLoadCubeTexture`
- [ ] `veImportExternalTexture`
- [ ] `veReleaseExternalTexture`
- [ ] `veHostWriteTextureRegion`
- [ ] `veHostReadTextureRegion`
- [ ] `veGetSparseTextureInfo`
- [ ] `veCommitSparsePages`
- [ ] `veUncommitSparsePages`
- [ ] `veFlushSparseBindings`
- [ ] `veIsSparsePagesCommitted`
- [ ] `veGetSparseCommittedPageCount`
- [ ] `veGetSparseTotalPageCount`
- [ ] `veCmdGenerateMipmaps`
- [ ] `veGenerateMipmaps`
- [ ] `veSaveTexture`
- [ ] `veCmdCopyTexture`
- [ ] `veCopyTexture`
- [ ] `veCmdBlitTexture`
- [ ] `veBlitTexture`
- [ ] `veCmdCopyBufferToTexture`
- [ ] `veCopyBufferToTexture`
- [ ] `veCmdCopyTextureToBuffer`
- [ ] `veCopyTextureToBuffer`

### Samplers

- [ ] `veCreateSampler`
- [ ] `veDestroySampler`
- [ ] `veCreateLinearSampler`
- [ ] `veCreateNearestSampler`
- [ ] `veCreateAnisotropicSampler`
- [ ] `veCreateShadowSampler`
- [ ] `veGetDefaultSampler`

### Shader API

- [ ] `veLoadShaderFromBuffer`
- [ ] `veLoadShaderFromFile`
- [ ] `veDestroyShader`
- [ ] `veSetShaderHotReloadEnabled`
- [ ] `veShaderNeedsReload`
- [ ] `veReloadShader`

### Graphics Pipeline / Draw State

- [ ] `veCreateGraphicsPipeline`
- [ ] `veDestroyGraphicsPipeline`
- [ ] `veCreateOpaquePipeline`
- [ ] `veCreateTransparentPipeline`
- [ ] `veCreateAdditivePipeline`
- [ ] `veCreateShadowPipeline`
- [ ] `veCreateUIOverlayPipeline`
- [ ] `veDefaultGraphicsPipelineDesc`
- [ ] `veCreateDrawState`
- [ ] `veDestroyDrawState`
- [ ] `veCreateDefaultDrawState`
- [ ] `veCreateWireframeDrawState`
- [ ] `veCreateShadowDrawState`
- [ ] `veDefaultDrawStateDesc`

### Command Buffers

- [ ] `veBeginCommandBuffer`
- [ ] `veSubmitCommandBuffer`
- [ ] `veEndCommandBuffer`
- [ ] `veResetCommandBuffer`
- [ ] `veReleaseCommandBuffer`
- [ ] `vePopulateSecondaryDescFromRenderTarget`
- [ ] `veBeginSecondaryCommandBuffer`
- [ ] `veBeginSecondaryRecording`
- [ ] `veExecuteSecondaryCommandBuffers`

### Render Targets / Rendering

- [ ] `veCreateRenderTarget`
- [ ] `veCreateRenderTargetWithOffset`
- [ ] `veRenderTargetAddColorAttachment`
- [ ] `veRenderTargetAddColorAttachmentResolve`
- [ ] `veRenderTargetSetDepthAttachment`
- [ ] `veRenderTargetSetStencilAttachment`
- [ ] `veResizeRenderTarget`
- [ ] `veCreateSimpleRenderTarget`
- [ ] `veBlitTextureToSwapchain`
- [ ] `veBeginRendering`
- [ ] `veEndRendering`

### State Binding / Dynamic State

- [ ] `veBindGraphicsPipeline`
- [ ] `veApplyDrawState`
- [ ] `veApplyGraphicsState`
- [ ] `veBindShader`
- [ ] `veBindShaders`
- [ ] `veUnbindShaderStage`
- [ ] `veSetViewport`
- [ ] `veSetScissor`
- [ ] `veSetTopology`
- [ ] `veSetPrimitiveRestart`
- [ ] `veSetPatchControlPoints`
- [ ] `veSetPolygonMode`
- [ ] `veSetLineWidth`
- [ ] `veSetCullMode`
- [ ] `veSetFrontFace`
- [ ] `veSetRasterizerDiscard`
- [ ] `veSetDepthBias`
- [ ] `veSetDepthClamp`
- [ ] `veSetAlphaToCoverage`
- [ ] `veSetAlphaToOne`
- [ ] `veSetDepthTest`
- [ ] `veSetDepthWrite`
- [ ] `veSetDepthCompareOp`
- [ ] `veSetStencilTest`
- [ ] `veSetStencilOp`
- [ ] `veSetStencilReference`
- [ ] `veSetWireframe`
- [ ] `veSetDepthTesting`

### Push Constants / Buffer Binding

- [ ] `vePushConstants`
- [ ] `veBindVertexBuffer`
- [ ] `veBindIndexBuffer`

### Draw Commands

- [ ] `veDraw`
- [ ] `veDrawIndexed`
- [ ] `veDrawIndirect`
- [ ] `veDrawIndexedIndirect`
- [ ] `veDrawIndirectCount`
- [ ] `veDrawIndexedIndirectCount`

### Mesh Shader Draw Commands

- [ ] `veDrawMeshTasks`
- [ ] `veDrawMeshTasksIndirect`
- [ ] `veDrawMeshTasksIndirectCount`

### Clear Commands

- [ ] `veClearColorAttachment`
- [ ] `veClearDepthStencilAttachment`

### Query API

- [ ] `veCreateQueryPool`
- [ ] `veDestroyQueryPool`
- [ ] `veCmdResetQueryPool`
- [ ] `veCmdBeginQuery`
- [ ] `veCmdEndQuery`
- [ ] `veCmdWriteTimestamp`
- [ ] `veGetQueryResults`

### Compute

- [ ] `veDispatch`
- [ ] `veDispatchIndirect`

### Barriers / Layout Transitions

- [ ] `veBarrierVertexToFragment`
- [ ] `veBarrierComputeToVertex`
- [ ] `veBarrierComputeToCompute`
- [ ] `veBarrierGraphicsToPresent`
- [ ] `veBarrier`
- [ ] `veTransitionTexture`
- [ ] `veTransitionTextureForShaderRead`
- [ ] `veTransitionTextureForColorAttachment`
- [ ] `veTransitionTextureForDepthAttachment`
- [ ] `veTransitionTextureForTransferSrc`
- [ ] `veTransitionTextureForTransferDst`
- [ ] `veTransitionTextureForPresent`
- [ ] `veTransitionTextureToLayout`

### Swapchains

- [ ] `veCreateSwapchain`
- [ ] `veDestroySwapchain`
- [ ] `vePresentImage`
- [ ] `veResizeSwapchain`
- [ ] `veGetSwapchainSize`
- [ ] `veGetSwapchainFormat`

### Debug / Profiling / Stats

- [ ] `veBeginDebugLabel`
- [ ] `veEndDebugLabel`
- [ ] `veInsertDebugLabel`
- [ ] `veSetBufferDebugName`
- [ ] `veSetTextureDebugName`
- [ ] `veSetSamplerDebugName`
- [ ] `veGetPerformanceStats`
- [ ] `veGetMemoryStats`
- [ ] `veGetPipelineStats`
- [ ] `vePrintDebugInfo`
- [ ] `vePrintProfileInfo`
- [ ] `vePrintGraphicsPipeline`
- [ ] `veValidateGraphicsPipeline`

### Frame Timing

- [ ] `veBeginFrame`
- [ ] `veEndFrame`

## Notes

- The script side already has several struct constructor helpers and enums; the
  native package surface is the real gap.
- Many of the missing APIs return or consume opaque handles like
  `VECommandBuffer*`, `VESwapchain*`, `VEQueryPool*`, `VEShader*`,
  `VEGraphicsPipeline*`, and `VEDrawState*`.
- Before grinding through the rest of the list, add userdata wrappers for those
  missing object types so the remaining APIs have a sane Mobius shape.
