# Mason geometry binding discovery inventory

Reviewed 2026-10-03. This inventory preserves context, command name and input from the unversioned reference artifact in the Claude handoff. It is a discovery aid, not a verified Hammer 5 default keymap and not a statement of Mason support.

Use [the verified workflow matrix](mason_geometry_workflow_research.md) for primary-source evidence, current Mason behavior and implementation prerequisites. The [complete CSV](mason_hammer_binding_discovery.csv) contains all 825 records across 53 contexts, including runtime/game-specific tools outside this geometry pass.

Reference artifact: `/tmp/twist.keybindings`; SHA-256 `d55c682c6fff6eb1479eef33d28034dad151191c8a4ef8162cc613ac636252a4`. No installed Hammer version is recorded. Mac/platform mappings, modifier macros and contextual command eligibility must be resolved before adopting any entry.

## Context inventory

| Reference context | Binding records | Geometry detail below |
| --- | ---: | --- |
| `HammerApp` | 35 | Full CSV |
| `HammerEditorSession` | 97 | Full CSV |
| `SessionCycleWidget` | 3 | Full CSV |
| `MapManifestWidget` | 1 | Full CSV |
| `HammerObjectPropertyPopup` | 3 | Full CSV |
| `MapView` | 5 | Yes |
| `PaneContainer` | 11 | Yes |
| `ToolPivotPicker` | 2 | Yes |
| `SelectionSetEditor` | 4 | Full CSV |
| `MapVariablesEditor` | 1 | Full CSV |
| `ToolBlock` | 7 | Yes |
| `ToolPolygon` | 6 | Yes |
| `ToolClipper` | 7 | Yes |
| `ToolTextureProjection` | 7 | Yes |
| `ToolMirror` | 6 | Yes |
| `ToolEntity` | 6 | Full CSV |
| `GizmoManipulator` | 7 | Yes |
| `SphereManipulator` | 1 | Yes |
| `AxisRotatorManipulator` | 2 | Yes |
| `BoxManipulator` | 3 | Yes |
| `RectangleManipulator` | 2 | Yes |
| `PointManipulator` | 1 | Yes |
| `PointOnLineManipulator` | 1 | Yes |
| `StaticPointManipulator` | 3 | Yes |
| `BarnLightManipulator` | 4 | Full CSV |
| `ToolFaceSelection` | 100 | Yes |
| `ToolVertexSelection` | 55 | Yes |
| `ToolEdgeSelection` | 70 | Yes |
| `ToolEdgeCut` | 12 | Yes |
| `ToolVertexNormalPaint` | 4 | Yes |
| `ToolEdgeArc` | 9 | Yes |
| `ToolFastTexture` | 20 | Yes |
| `ToolFastTexture_UVWidget` | 15 | Yes |
| `ToolBridge` | 9 | Yes |
| `ToolBevel` | 9 | Yes |
| `ToolFaceBevel` | 9 | Yes |
| `ToolThicken` | 8 | Yes |
| `ToolPainting` | 14 | Full CSV |
| `ToolStaticOverlay` | 25 | Full CSV |
| `ToolAssetSpray` | 3 | Full CSV |
| `ToolPhysics` | 18 | Full CSV |
| `ToolPath` | 6 | Full CSV |
| `ToolSelection` | 74 | Yes |
| `ToolmeshDisplacement` | 17 | Yes |
| `ToolPickEntity` | 5 | Full CSV |
| `ToolPlaceNavWalkable` | 2 | Full CSV |
| `ToolWorkPlane` | 8 | Yes |
| `ToolTerrain` | 6 | Full CSV |
| `TerrainGraphEditor` | 1 | Full CSV |
| `TerrainGraphEditorView` | 8 | Full CSV |
| `ToolMeshProjection` | 4 | Yes |
| `EntityOutputWidget` | 4 | Full CSV |
| `ToolDotaTileEditor` | 85 | Full CSV |

## Geometry and viewport records

Repeated commands and chords are intentional: scope changes their meaning. Contexts retain the spelling in the source artifact. Inputs containing macro names are left unexpanded. Unavailable Mason component tools must remain disabled until stable component picking, validation, material transfer and undo are connected.

### MapView

| Reference command | Reference input |
| --- | --- |
| `ShowContextMenu` | `RMouse` |
| `MaterialTraceLift` | `Shift+RMouse` |
| `MaterialTraceLift` | `Ctrl+M` |
| `SnapCameraToTargetUnderMouse` | `Shift+Alt+A` |
| `DetachViewFromObject` | `Ctrl+Backspace` |

### PaneContainer

| Reference command | Reference input |
| --- | --- |
| `2D_Top` | `F2` |
| `2D_Front` | `F3` |
| `2D_Side` | `F4` |
| `3D_FullBrightNoLighting` | `F5` |
| `3D_AllLighting` | `F6` |
| `Rtx_Path_Tracing` | `Ctrl+Shift+Alt+F11` |
| `3D_ToolsVis` | `F7` |
| `ToggleWireframeOverlay` | `F8` |
| `ToggleShadows` | `F10` |
| `ToggleMeshEdges` | `F11` |
| `RTX_Visualization` | `Ctrl+Shift+Alt+F10` |

### ToolPivotPicker

| Reference command | Reference input |
| --- | --- |
| `PlacePivot` | `LMouse` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |

### ToolBlock

| Reference command | Reference input |
| --- | --- |
| `DragBlock` | `LMouse` |
| `Finish` | `Enter` |
| `Finish` | `NumEnter` |
| `Apply` | `Space` |
| `CancelCreate` | `Esc` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |
| `AlignToSurface` | `Shift` |

### ToolPolygon

| Reference command | Reference input |
| --- | --- |
| `AddPoint` | `LMouse` |
| `Finish` | `Enter` |
| `Finish` | `NumEnter` |
| `Apply` | `LMouseDoubleClick` |
| `Apply` | `Space` |
| `Cancel` | `Esc` |

### ToolClipper

| Reference command | Reference input |
| --- | --- |
| `DragClip` | `LMouse` |
| `FinishClip` | `Enter` |
| `FinishClip` | `NumEnter` |
| `DoClip` | `Space` |
| `CancelClip` | `Esc` |
| `ToggleCreateCaps` | `Ctrl+Shift+X` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |

### ToolTextureProjection

| Reference command | Reference input |
| --- | --- |
| `Finish` | `Enter` |
| `Finish` | `NumEnter` |
| `Finish` | `Space` |
| `Cancel` | `Esc` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |
| `Translate` | `Shift+T` |
| `Rotate` | `Shift+R` |

### ToolMirror

| Reference command | Reference input |
| --- | --- |
| `DragPlane` | `LMouse` |
| `Finish` | `Enter` |
| `Finish` | `NumEnter` |
| `Apply` | `Space` |
| `Cancel` | `Esc` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |

### GizmoManipulator

| Reference command | Reference input |
| --- | --- |
| `Drag` | `LMouse` |
| `StampClone` | `Shift` |
| `SetBoxManipulateAboutCenter` | `K` |
| `SetBoxManipulateApplyUniformScaling` | `J` |
| `LockedManipulation` | `Shift` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |
| `GizmoApplyValue` | `NumEnter` |

### SphereManipulator

| Reference command | Reference input |
| --- | --- |
| `Drag` | `LMouse` |

### AxisRotatorManipulator

| Reference command | Reference input |
| --- | --- |
| `Drag` | `LMouse` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |

### BoxManipulator

| Reference command | Reference input |
| --- | --- |
| `Drag` | `LMouse` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |
| `ToggleEditEdgeFades` | `Shift` |

### RectangleManipulator

| Reference command | Reference input |
| --- | --- |
| `Drag` | `LMouse` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |

### PointManipulator

| Reference command | Reference input |
| --- | --- |
| `SelectPoint` | `LMouse` |

### PointOnLineManipulator

| Reference command | Reference input |
| --- | --- |
| `Drag` | `LMouse` |

### StaticPointManipulator

| Reference command | Reference input |
| --- | --- |
| `Click` | `LMouse` |
| `MouseWheelUp` | `MWheelUp` |
| `MouseWheelDown` | `MWheelDn` |

### ToolFaceSelection

| Reference command | Reference input |
| --- | --- |
| `ShrinkSelection` | `NumSub` |
| `ShrinkSelection` | `-` |
| `GrowSelection` | `NumAdd` |
| `GrowSelection` | `=` |
| `AddToSelection` | `Ctrl+Up` |
| `RemoveFromSelection` | `Ctrl+Down` |
| `RemoveFromSelectionOldest` | `Ctrl+Alt+Down` |
| `FlipNormals` | `F` |
| `SelectLoop` | `L` |
| `CutTool` | `C` |
| `QuadSlice` | `Ctrl+D` |
| `JustifyCenter` | `Ctrl+Shift+Ins` |
| `JustifyLeft` | `Ctrl+Shift+Home` |
| `JustifyRight` | `Ctrl+Shift+End` |
| `JustifyTop` | `Ctrl+Shift+PgUp` |
| `JustifyBottom` | `Ctrl+Shift+PgDn` |
| `JustifyFit` | `Ctrl+Shift+Del` |
| `AlignWorld` | `Ctrl+Shift+T` |
| `AlignFace` | `Ctrl+Shift+F` |
| `AlignView` | `V` |
| `Select` | `LMouse` |
| `LassoSelect` | `MMouse` |
| `TranslateLasso` | `Space` |
| `SelectionAddModifier` | `SELECTION_ADD_KEY` |
| `SelectionRemoveModifier` | `SELECTION_REMOVE_KEY` |
| `Extrude` | `Shift` |
| `Constrain` | `Alt` |
| `SelectContiguous` | `LMouseDoubleClick` |
| `SelectContiguousFiltered` | `Alt+LMouseDoubleClick` |
| `WrapTexture` | `Alt+RMouse` |
| `WrapTextureToSelection` | `Shift+Alt+RMouse` |
| `TraceAndLiftMaterial` | `Shift+RMouse` |
| `PaintMaterial` | `Ctrl+RMouse` |
| `NudgeUp` | `Up` |
| `NudgeRotateUp` | `Alt+Up` |
| `NudgeDown` | `Down` |
| `NudgeRotateDown` | `Alt+Down` |
| `NudgeLeft` | `Left` |
| `NudgeRotateLeft` | `Alt+Left` |
| `NudgeRight` | `Right` |
| `NudgeRotateRight` | `Alt+Right` |
| `ApplyMaterialToSelection` | `Shift+T` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |
| `Bridge` | `B` |
| `BridgeTool` | `Alt+B` |
| `ThickenTool` | `G` |
| `FaceBevelTool` | `Ctrl+F` |
| `MoveToFurthestFace` | `Alt+W` |
| `Mirror` | `M` |
| `NudgeTextureRight` | `Mouse5` |
| `NudgeTextureLeft` | `Mouse4` |
| `NudgeTextureUp` | `Shift+Mouse5` |
| `NudgeTextureDown` | `Shift+Mouse4` |
| `JustifyRight` | `Ctrl+Mouse5` |
| `JustifyLeft` | `Ctrl+Mouse4` |
| `JustifyTop` | `Ctrl+Shift+Mouse5` |
| `JustifyBottom` | `Ctrl+Shift+Mouse4` |
| `Combine` | `Backspace` |
| `Collapse` | `O` |
| `Detach` | `N` |
| `Extract` | `Alt+N` |
| `ClearPivot` | `Home` |
| `CenterPivotInView` | `Ctrl+Home` |
| `SetPivotToWorldOrigin` | `Ctrl+End` |
| `EndPivotManipulation` | `Ins` |
| `IncreaseSubdivisionLevel` | `,` |
| `DecreaseSubdivisionLevel` | `.` |
| `MoveObjectDownByTraceLocal` | `Ctrl+Num1` |
| `MoveObjectDownByTrace` | `Ctrl+Num2` |
| `NextTile` | `Alt+F` |
| `PreviousTile` | `Alt+V` |
| `RandomTile` | `Alt+G` |
| `RotateTexture90CCW` | `Alt+Q` |
| `RotateTexture90CW` | `Alt+A` |
| `FlipTextureHorizontal` | `Alt+D` |
| `FlipTextureVertical` | `Alt+E` |
| `ApplyMaterialByHotspot` | `Alt+H` |
| `FastTextureTool` | `Ctrl+G` |
| `ToggleHotspotApplyMaterial` | `Shift+Alt+G` |
| `ToggleHotspotTilingMode` | `Shift+Alt+T` |
| `ToggleHotspotAllowRandom` | `Shift+Alt+R` |
| `ToggleHotspotPerFace` | `Shift+Alt+F` |
| `ToggleHotspotMappingMode` | `Shift+Alt+D` |
| `RadialAlign` | `Alt+S` |
| `ReflectPie` | `Ctrl+Alt+W` |
| `GizmoNumKey0` | `Num0` |
| `GizmoNumKey1` | `Num1` |
| `GizmoNumKey2` | `Num2` |
| `GizmoNumKey3` | `Num3` |
| `GizmoNumKey4` | `Num4` |
| `GizmoNumKey5` | `Num5` |
| `GizmoNumKey6` | `Num6` |
| `GizmoNumKey7` | `Num7` |
| `GizmoNumKey8` | `Num8` |
| `GizmoNumKey9` | `Num9` |
| `GizmoNumKeyDecimal` | `NumDec` |
| `GizmoComma` | `,` |
| `GizmoNumKeyMinus` | `NumSub` |
| `GizmoClearValue` | `Backspace` |
| `GizmoApplyValue` | `NumEnter` |

### ToolVertexSelection

| Reference command | Reference input |
| --- | --- |
| `Select` | `LMouse` |
| `LassoSelect` | `MMouse` |
| `TranslateLasso` | `Space` |
| `SelectionAddModifier` | `SELECTION_ADD_KEY` |
| `SelectionRemoveModifier` | `SELECTION_REMOVE_KEY` |
| `GrowSelection` | `NumAdd` |
| `GrowSelection` | `=` |
| `ShrinkSelection` | `NumSub` |
| `ShrinkSelection` | `-` |
| `AddToSelection` | `Ctrl+Up` |
| `RemoveFromSelection` | `Ctrl+Down` |
| `RemoveFromSelectionOldest` | `Ctrl+Alt+Down` |
| `SelectLoop` | `L` |
| `SelectContiguous` | `LMouseDoubleClick` |
| `CutTool` | `C` |
| `CreateNewEdge` | `V` |
| `Merge` | `M` |
| `MergeTarget` | `Ctrl+M` |
| `MergeFixed` | `Shift+M` |
| `MoveToFurthestVertex` | `Alt+W` |
| `SnapToVertex` | `B` |
| `NudgeUp` | `Up` |
| `NudgeRotateUp` | `Alt+Up` |
| `NudgeDown` | `Down` |
| `NudgeRotateDown` | `Alt+Down` |
| `NudgeLeft` | `Left` |
| `NudgeRotateLeft` | `Alt+Left` |
| `NudgeRight` | `Right` |
| `NudgeRotateRight` | `Alt+Right` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |
| `ClearPivot` | `Home` |
| `CenterPivotInView` | `Ctrl+Home` |
| `SetPivotToWorldOrigin` | `Ctrl+End` |
| `EndPivotManipulation` | `Ins` |
| `Bevel` | `F` |
| `Extrude` | `Shift` |
| `Constrain` | `Alt` |
| `MoveObjectDownByTraceLocal` | `Ctrl+Num1` |
| `MoveObjectDownByTrace` | `Ctrl+Num2` |
| `WeldUVs` | `Ctrl+F` |
| `GizmoNumKey0` | `Num0` |
| `GizmoNumKey1` | `Num1` |
| `GizmoNumKey2` | `Num2` |
| `GizmoNumKey3` | `Num3` |
| `GizmoNumKey4` | `Num4` |
| `GizmoNumKey5` | `Num5` |
| `GizmoNumKey6` | `Num6` |
| `GizmoNumKey7` | `Num7` |
| `GizmoNumKey8` | `Num8` |
| `GizmoNumKey9` | `Num9` |
| `GizmoNumKeyDecimal` | `NumDec` |
| `GizmoComma` | `,` |
| `GizmoNumKeyMinus` | `NumSub` |
| `GizmoClearValue` | `Backspace` |
| `GizmoApplyValue` | `NumEnter` |

### ToolEdgeSelection

| Reference command | Reference input |
| --- | --- |
| `Select` | `LMouse` |
| `LassoSelect` | `MMouse` |
| `TranslateLasso` | `Space` |
| `SelectionAddModifier` | `SELECTION_ADD_KEY` |
| `SelectionRemoveModifier` | `SELECTION_REMOVE_KEY` |
| `ShrinkSelection` | `NumSub` |
| `ShrinkSelection` | `-` |
| `GrowSelection` | `NumAdd` |
| `GrowSelection` | `=` |
| `AddToSelection` | `Ctrl+Up` |
| `RemoveFromSelection` | `Ctrl+Down` |
| `RemoveFromSelectionOldest` | `Ctrl+Alt+Down` |
| `CutTool` | `C` |
| `ArcTool` | `Y` |
| `SelectSingleLoop` | `LMouseDoubleClick` |
| `SelectLoop` | `L` |
| `SelectRing` | `G` |
| `SelectRibs` | `Ctrl+G` |
| `CreatePolygonFromEdges` | `P` |
| `Dissolve` | `Backspace` |
| `DissolveAgressive` | `Shift+Backspace` |
| `Bridge` | `B` |
| `BridgeTool` | `Alt+B` |
| `Merge` | `M` |
| `Split` | `Alt+N` |
| `SnapEdgeToEdge` | `I` |
| `MoveToFurthestEdge` | `Alt+W` |
| `Extrude` | `X` |
| `Extend` | `N` |
| `Connect` | `V` |
| `Bevel` | `Alt+F` |
| `BevelTool` | `F` |
| `Collapse` | `O` |
| `SetNormalsHard` | `H` |
| `SetNormalsSoft` | `J` |
| `SetNormalsDefault` | `K` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |
| `TransformExtrude` | `Shift` |
| `Constrain` | `Alt` |
| `ClearPivot` | `Home` |
| `CenterPivotInView` | `Ctrl+Home` |
| `SetPivotToWorldOrigin` | `Ctrl+End` |
| `EndPivotManipulation` | `Ins` |
| `MoveObjectDownByTraceLocal` | `Ctrl+Num1` |
| `MoveObjectDownByTrace` | `Ctrl+Num2` |
| `WeldUVs` | `Ctrl+F` |
| `RadialAlign` | `Alt+S` |
| `NudgeUp` | `Up` |
| `NudgeRotateUp` | `Alt+Up` |
| `NudgeDown` | `Down` |
| `NudgeRotateDown` | `Alt+Down` |
| `NudgeLeft` | `Left` |
| `NudgeRotateLeft` | `Alt+Left` |
| `NudgeRight` | `Right` |
| `NudgeRotateRight` | `Alt+Right` |
| `GizmoNumKey0` | `Num0` |
| `GizmoNumKey1` | `Num1` |
| `GizmoNumKey2` | `Num2` |
| `GizmoNumKey3` | `Num3` |
| `GizmoNumKey4` | `Num4` |
| `GizmoNumKey5` | `Num5` |
| `GizmoNumKey6` | `Num6` |
| `GizmoNumKey7` | `Num7` |
| `GizmoNumKey8` | `Num8` |
| `GizmoNumKey9` | `Num9` |
| `GizmoNumKeyDecimal` | `NumDec` |
| `GizmoComma` | `,` |
| `GizmoNumKeyMinus` | `NumSub` |
| `GizmoClearValue` | `Backspace` |
| `GizmoApplyValue` | `NumEnter` |

### ToolEdgeCut

| Reference command | Reference input |
| --- | --- |
| `CutEdge` | `LMouse` |
| `Finish` | `Enter` |
| `Finish` | `NumEnter` |
| `Apply` | `Space` |
| `Cancel` | `Esc` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |
| `AlignedSnap` | `Shift` |
| `PlaceCut` | `B` |
| `PlaceCut` | `Ctrl+B` |
| `ToggleLoopCut` | `V` |
| `ToggleLoopCutEven` | `E` |
| `ToggleLoopCutFlip` | `F` |

### ToolVertexNormalPaint

| Reference command | Reference input |
| --- | --- |
| `Paint` | `LMouse` |
| `Erase` | `Ctrl` |
| `ScaleBrush` | `MMouse` |
| `FreezeNormal` | `Shift` |

### ToolEdgeArc

| Reference command | Reference input |
| --- | --- |
| `AdjustParameters` | `LMouse` |
| `Finish` | `Enter` |
| `Finish` | `NumEnter` |
| `Apply` | `Space` |
| `Cancel` | `Esc` |
| `LockToAxis` | `Shift` |
| `IncreaseNumSteps` | `F` |
| `DecreaseNumSteps` | `V` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |

### ToolFastTexture

| Reference command | Reference input |
| --- | --- |
| `Finish` | `Enter` |
| `Finish` | `NumEnter` |
| `Finish` | `Space` |
| `Finish` | `Ctrl+G` |
| `Cancel` | `Esc` |
| `ToggleFlipHorizontal` | `F` |
| `ToggleFlipVertical` | `V` |
| `TogglePrimaryAxis` | `X` |
| `MappingUnwrapSquare` | `1` |
| `MappingUnwrapConforming` | `2` |
| `MappingUnwrapQuads` | `3` |
| `MappingPlanar` | `4` |
| `MappingUseExisting` | `5` |
| `SmallerGrid` | `[` |
| `BiggerGrid` | `]` |
| `DecreaseInset` | `NumSub` |
| `IncreaseInset` | `NumAdd` |
| `DecreaseInset` | `-` |
| `IncreaseInset` | `=` |
| `FitSelectionInView` | `Shift+A` |

### ToolFastTexture_UVWidget

| Reference command | Reference input |
| --- | --- |
| `ResetMappingBounds` | `Shift+LMouseDoubleClick` |
| `PickRectangle` | `LMouseDoubleClick` |
| `Drag` | `LMouse` |
| `Drag` | `Alt+LMouse` |
| `Noop` | `MMouse` |
| `NudgeUp` | `Up` |
| `NudgeDown` | `Down` |
| `NudgeLeft` | `Left` |
| `NudgeRight` | `Right` |
| `BeginPickEdge` | `E` |
| `CancelPickEdge` | `Esc` |
| `PickEdge` | `LMouse` |
| `DrawRectangle` | `Alt` |
| `DisableSnapping` | `TOGGLE_SNAPPING_KEY` |
| `ResetView` | `Home` |

### ToolBridge

| Reference command | Reference input |
| --- | --- |
| `AdjustParameters` | `LMouse` |
| `Finish` | `Enter` |
| `Finish` | `NumEnter` |
| `Apply` | `Space` |
| `Cancel` | `Esc` |
| `IncreaseNumSteps` | `F` |
| `DecreaseNumSteps` | `V` |
| `FreeDrag` | `Shift` |
| `LockPoints` | `Ctrl` |

### ToolBevel

| Reference command | Reference input |
| --- | --- |
| `Finish` | `Enter` |
| `Finish` | `NumEnter` |
| `Finish` | `LMouse` |
| `Apply` | `Space` |
| `Cancel` | `Esc` |
| `AdjustSteps` | `C` |
| `AdjustWidth` | `F` |
| `AdjustShape` | `V` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |

### ToolFaceBevel

| Reference command | Reference input |
| --- | --- |
| `Finish` | `Enter` |
| `Finish` | `NumEnter` |
| `Finish` | `LMouse` |
| `Apply` | `Space` |
| `Cancel` | `Esc` |
| `AdjustExtrude` | `F` |
| `AdjustInset` | `V` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |
| `ToggleEachFace` | `X` |

### ToolThicken

| Reference command | Reference input |
| --- | --- |
| `Finish` | `Enter` |
| `Finish` | `NumEnter` |
| `Finish` | `LMouse` |
| `Apply` | `Space` |
| `Cancel` | `Esc` |
| `AdjustThickness` | `F` |
| `ToggleFromCenter` | `X` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |

### ToolSelection

| Reference command | Reference input |
| --- | --- |
| `Select` | `LMouse` |
| `BoxSelect` | `LMouse` |
| `ActivateObject` | `LMouseDoubleClick` |
| `LassoSelect` | `MMouse` |
| `TranslateLasso` | `Space` |
| `MoveSelected` | `LMouse` |
| `SelectAndMove2D` | `Alt+LMouse` |
| `AddObjectToSelection` | `SELECTION_ADD_KEY` |
| `RemoveObjectFromSelection` | `SELECTION_REMOVE_KEY` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |
| `StampClone` | `Shift` |
| `CloneSelection` | `Shift` |
| `ToggleAlignToSurface` | `Alt` |
| `SelectNext` | `PgUp` |
| `SelectPrev` | `PgDn` |
| `CancelSelectOperation` | `Esc` |
| `ShrinkGizmo` | `Ctrl+NumSub` |
| `EnlargeGizmo` | `Ctrl+NumAdd` |
| `NextPivotTap` | `N` |
| `NextPivotCycle` | `N+MWheelUp` |
| `PrevPivotCycle` | `N+MWheelDn` |
| `ClearPivot` | `Home` |
| `CenterPivotInView` | `Ctrl+Home` |
| `SetPivotToWorldOrigin` | `Ctrl+End` |
| `SetPivotToSelectionCenter` | `Alt+Home` |
| `EndPivotManipulation` | `Ins` |
| `FlipNormals` | `F` |
| `MergeSelectedMeshes` | `M` |
| `SeparateMeshComponents` | `Alt+N` |
| `NudgeUp` | `Up` |
| `NudgeRotateUp` | `Alt+Up` |
| `NudgeDown` | `Down` |
| `NudgeRotateDown` | `Alt+Down` |
| `NudgeLeft` | `Left` |
| `NudgeRotateLeft` | `Alt+Left` |
| `NudgeRight` | `Right` |
| `NudgeRotateRight` | `Alt+Right` |
| `ApplyMaterial` | `Shift+T` |
| `SetOriginToCenter` | `End` |
| `ClearRotationAndScale` | `Ctrl+Num0` |
| `MoveObjectDownByTraceLocal` | `Ctrl+Num1` |
| `MoveObjectDownByTrace` | `Ctrl+Num2` |
| `ShowObjectProperties` | `Alt+Enter` |
| `GrowSelection` | `NumAdd` |
| `GrowSelection` | `=` |
| `ShrinkSelection` | `NumSub` |
| `ShrinkSelection` | `-` |
| `SetOriginToPivot` | `Ctrl+D` |
| `AlignObjects` | `Alt+T` |
| `AlignObjectsRotation` | `Alt+R` |
| `AlignObjectsToWorkplane` | `Alt+E` |
| `AlignWorkplaneToObject` | `Alt+Q` |
| `NextVariation` | `Alt+F` |
| `PreviousVariation` | `Alt+V` |
| `HideElement` | `Alt+B` |
| `ResetConfiguration` | `Alt+M` |
| `ReevaluateConfiguration` | `Ctrl+F` |
| `BakeMeshDeformers` | `Ctrl+L` |
| `ToggleDeformation` | `Alt+D` |
| `GizmoNumKey0` | `Num0` |
| `GizmoNumKey1` | `Num1` |
| `GizmoNumKey2` | `Num2` |
| `GizmoNumKey3` | `Num3` |
| `GizmoNumKey4` | `Num4` |
| `GizmoNumKey5` | `Num5` |
| `GizmoNumKey6` | `Num6` |
| `GizmoNumKey7` | `Num7` |
| `GizmoNumKey8` | `Num8` |
| `GizmoNumKey9` | `Num9` |
| `GizmoNumKeyDecimal` | `NumDec` |
| `GizmoComma` | `,` |
| `GizmoNumKeyMinus` | `NumSub` |
| `GizmoClearValue` | `Backspace` |
| `GizmoApplyValue` | `NumEnter` |

### ToolmeshDisplacement

| Reference command | Reference input |
| --- | --- |
| `ModePushPull` | `F` |
| `ModeFlatten` | `G` |
| `ModeMove` | `H` |
| `ModeInflate` | `Z` |
| `ModeClay` | `C` |
| `ModePinch` | `V` |
| `ModeErase` | `X` |
| `ModeSmooth` | `B` |
| `PickNormal` | `Ctrl+RMouse` |
| `ResetNormal` | `Ctrl+Alt+RMouse` |
| `Paint` | `LMouse` |
| `AdjustRadius` | `MMouse` |
| `SmoothModifier` | `Shift` |
| `EraseModifer` | `Ctrl+Shift` |
| `ReverseModifier` | `Ctrl` |
| `DropTool` | `Space` |
| `DropTool` | `Esc` |

### ToolWorkPlane

| Reference command | Reference input |
| --- | --- |
| `PickWorkPlaneFromSurface` | `LMouse` |
| `PickWorkPlaneFromObject` | `Shift+LMouse` |
| `WorkPlaneModeTranslate` | `Shift+T` |
| `WorkPlaneModeRotate` | `Shift+R` |
| `ToggleSnapping` | `TOGGLE_SNAPPING_KEY` |
| `ResetWorkPlaneAndDropTool` | `Esc` |
| `DropTool` | `Space` |
| `DropTool` | `Enter` |

### ToolMeshProjection

| Reference command | Reference input |
| --- | --- |
| `Finish` | `Enter` |
| `Finish` | `NumEnter` |
| `UpdateProjection` | `Space` |
| `Cancel` | `Esc` |
