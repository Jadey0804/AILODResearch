import unreal


MAP_PATH = "/Game/Phase7/Maps/L_Phase7_VisualDemo"
TARGET_TILE_POOL_SIZE = 32768


world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
if world is None:
    raise RuntimeError(f"Failed to load map: {MAP_PATH}")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
recast_actors = [
    actor
    for actor in actor_subsystem.get_all_level_actors()
    if actor.get_class().get_name() == "RecastNavMesh"
]
if len(recast_actors) != 1:
    raise RuntimeError(f"Expected exactly one RecastNavMesh, found {len(recast_actors)}")

recast = recast_actors[0]
nav_package = recast.get_package()
unreal.log(f"PHASE7F_NAV_NAME={recast.get_name()}")
unreal.log(f"PHASE7F_NAV_PATH={recast.get_path_name()}")
unreal.log(f"PHASE7F_NAV_PACKAGE={nav_package.get_name()}")
unreal.log(f"PHASE7F_NAV_OLD_TILE_POOL={recast.get_editor_property('tile_pool_size')}")
unreal.log(f"PHASE7F_NAV_OLD_FIXED_POOL={recast.get_editor_property('fixed_tile_pool_size')}")

recast.modify()
recast.set_editor_property("fixed_tile_pool_size", True)
recast.set_editor_property("tile_pool_size", TARGET_TILE_POOL_SIZE)

if not unreal.EditorLoadingAndSavingUtils.save_packages([nav_package], True):
    raise RuntimeError(f"Failed to save NavMesh package: {nav_package.get_name()}")

unreal.log(f"PHASE7F_NAV_NEW_TILE_POOL={recast.get_editor_property('tile_pool_size')}")
unreal.log(f"PHASE7F_NAV_NEW_FIXED_POOL={recast.get_editor_property('fixed_tile_pool_size')}")
