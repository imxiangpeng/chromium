// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_HOST_HOST_FRAME_SINK_MANAGER_H_
#define COMPONENTS_VIZ_HOST_HOST_FRAME_SINK_MANAGER_H_

#include <memory>

#include "base/compiler_specific.h"
#include "base/containers/flat_map.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/optional.h"
#include "components/viz/common/surfaces/frame_sink_id.h"
#include "components/viz/host/frame_sink_observer.h"
#include "components/viz/host/viz_host_export.h"
#include "components/viz/service/frame_sinks/compositor_frame_sink_support_manager.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "services/viz/compositing/privileged/interfaces/frame_sink_manager.mojom.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace viz {

class CompositorFrameSinkSupport;
class CompositorFrameSinkSupportClient;
class FrameSinkManagerImpl;
class SurfaceInfo;

namespace test {
class HostFrameSinkManagerTest;
}

// Browser side wrapper of mojom::FrameSinkManager, to be used from the
// UI thread. Manages frame sinks and is intended to replace all usage of
// FrameSinkManagerImpl.
class VIZ_HOST_EXPORT HostFrameSinkManager
    : public NON_EXPORTED_BASE(mojom::FrameSinkManagerClient),
      public NON_EXPORTED_BASE(CompositorFrameSinkSupportManager) {
 public:
  HostFrameSinkManager();
  ~HostFrameSinkManager() override;

  // Sets a local FrameSinkManagerImpl instance and connects directly to it.
  void SetLocalManager(FrameSinkManagerImpl* frame_sink_manager_impl);

  // Binds |this| as a FrameSinkManagerClient for |request| on |task_runner|. On
  // Mac |task_runner| will be the resize helper task runner. May only be called
  // once.
  void BindAndSetManager(
      mojom::FrameSinkManagerClientRequest request,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner,
      mojom::FrameSinkManagerPtr ptr);

  void AddObserver(FrameSinkObserver* observer);
  void RemoveObserver(FrameSinkObserver* observer);

  // Creates a connection between client to viz, using |request| and |client|,
  // that allows the client to submit CompositorFrames. When no longer needed,
  // call DestroyCompositorFrameSink().
  void CreateCompositorFrameSink(const FrameSinkId& frame_sink_id,
                                 mojom::CompositorFrameSinkRequest request,
                                 mojom::CompositorFrameSinkClientPtr client);

  // Destroys a client connection. Will call UnregisterFrameSinkHierarchy() with
  // the registered parent if there is one.
  void DestroyCompositorFrameSink(const FrameSinkId& frame_sink_id);

  // Registers FrameSink hierarchy. Clients can call this multiple times to
  // reparent without calling UnregisterFrameSinkHierarchy().
  void RegisterFrameSinkHierarchy(const FrameSinkId& parent_frame_sink_id,
                                  const FrameSinkId& child_frame_sink_id);

  // Unregisters FrameSink hierarchy. Client must have registered FrameSink
  // hierarchy before unregistering.
  void UnregisterFrameSinkHierarchy(const FrameSinkId& parent_frame_sink_id,
                                    const FrameSinkId& child_frame_sink_id);

  // CompositorFrameSinkSupportManager:
  std::unique_ptr<CompositorFrameSinkSupport> CreateCompositorFrameSinkSupport(
      CompositorFrameSinkSupportClient* client,
      const FrameSinkId& frame_sink_id,
      bool is_root,
      bool handles_frame_sink_id_invalidation,
      bool needs_sync_points) override;

 private:
  friend class test::HostFrameSinkManagerTest;

  struct FrameSinkData {
    FrameSinkData();
    FrameSinkData(FrameSinkData&& other);
    ~FrameSinkData();
    FrameSinkData& operator=(FrameSinkData&& other);

    bool HasCompositorFrameSinkData() const {
      return has_created_compositor_frame_sink || support;
    }

    // Returns true if there is nothing in FrameSinkData and it can be deleted.
    bool IsEmpty() const {
      return !HasCompositorFrameSinkData() && !parent.has_value();
    }

    // If the frame sink is a root that corresponds to a Display.
    bool is_root = false;

    // The FrameSinkId registered as the parent in the BeginFrame hierarchy.
    base::Optional<FrameSinkId> parent;

    // If a mojom::CompositorFrameSink was created for this FrameSinkId. This
    // will always be false if not using Mojo.
    bool has_created_compositor_frame_sink = false;

    // This will be null if using Mojo.
    CompositorFrameSinkSupport* support = nullptr;

   private:
    DISALLOW_COPY_AND_ASSIGN(FrameSinkData);
  };

  // Assigns the temporary reference to the frame sink that is expected to
  // embeded |surface_id|, otherwise drops the temporary reference.
  void PerformAssignTemporaryReference(const SurfaceId& surface_id);

  // mojom::FrameSinkManagerClient:
  void OnSurfaceCreated(const SurfaceInfo& surface_info) override;
  void OnClientConnectionClosed(const FrameSinkId& frame_sink_id) override;

  // This will point to |frame_sink_manager_ptr_| if using Mojo or
  // |frame_sink_manager_impl_| if directly connected. Use this to make function
  // calls.
  mojom::FrameSinkManager* frame_sink_manager_ = nullptr;

  // Mojo connection to the FrameSinkManager. If this is bound then
  // |frame_sink_manager_impl_| must be null.
  mojom::FrameSinkManagerPtr frame_sink_manager_ptr_;

  // Mojo connection back from the FrameSinkManager.
  mojo::Binding<mojom::FrameSinkManagerClient> binding_;

  // A direct connection to FrameSinkManagerImpl. If this is set then
  // |frame_sink_manager_ptr_| must be unbound. For use in browser process only,
  // viz process should not set this.
  FrameSinkManagerImpl* frame_sink_manager_impl_ = nullptr;

  // Per CompositorFrameSink data.
  base::flat_map<FrameSinkId, FrameSinkData> frame_sink_data_map_;

  // Local observers to that receive OnSurfaceCreated() messages from IPC.
  base::ObserverList<FrameSinkObserver> observers_;

  base::WeakPtrFactory<HostFrameSinkManager> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(HostFrameSinkManager);
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_HOST_HOST_FRAME_SINK_MANAGER_H_
