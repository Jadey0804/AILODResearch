# AILOD Dear ImGui UE adapter provenance

- Adapter repository: `https://github.com/VesCodes/ImGui`
- Adapter commit: `71f29cf675b7d501af4f8e152fb73145f24a3f58`
- Adapter version: `1.3`
- Adapter commit date: 2026-05-22
- Bundled Dear ImGui: `1.92.8`, upstream commit `b61e56346a92cfcaf1f43a545ca37b0b32239654`
- Retrieved: 2026-08-23
- Intended engine: Unreal Engine 5.4

The adapter, Dear ImGui, ImPlot, and NetImGui license files are preserved in this directory. All four use the MIT License.

AILOD Phase 7C uses one in-viewport functional window. Project code disables Dear ImGui docking and multi-viewports and does not call ImPlot or NetImGui.

The vendored adapter has one UE5.4 compatibility patch in `Source/ImGui/Private/ImGuiContext.cpp`: upstream `MakeConstArrayView` is replaced by the equivalent `TConstArrayView64<uint8>` constructor accepted by the UE5.4 `UTexture2D::CreateTransient` API. No other upstream source is changed.
