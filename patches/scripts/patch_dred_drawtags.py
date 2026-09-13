import os, sys
ROOT = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"

def edit(rel, pairs):
    p = os.path.join(ROOT, rel)
    d = open(p, "rb").read(); crlf = b"\r\n" in d
    t = d.decode("utf-8").replace("\r\n", "\n")
    for old, new in pairs:
        if t.count(old) != 1:
            sys.exit("%s: %d matches for %r" % (rel, t.count(old), old[:70]))
        t = t.replace(old, new)
    open(p, "wb").write((t.replace("\n", "\r\n") if crlf else t).encode("utf-8"))
    print("patched", rel)

# --- deferred command list: a draw tag command + a ring of executed draws ------
edit("include/rex/graphics/d3d12/deferred_command_list.h", [
    ("#include <vector>\n", "#include <array>\n#include <vector>\n"),
    ("    kInsertDebugMarker,\n  };\n", "    kInsertDebugMarker,\n    kDrawTag,\n  };\n"),
    ("  void EndDebugMarker() { WriteCommand(Command::kEndDebugMarker, 0); }\n",
     "  // [dred] Naming a hung draw. The command processor tags each draw with its\n"
     "  // shaders (not a GPU op); Execute records the tag with the draw's op index\n"
     "  // - counting the same ops DRED's auto-breadcrumbs count - into a ring the\n"
     "  // device-loss handler reads. Two GPU hangs in one day of play (Fable II,\n"
     "  // 2026-09-13) gave \"DrawIndexedInstanced hung\" and nothing more.\n"
     "  struct DrawTagInfo {\n"
     "    uint64_t vertex_shader_hash;\n"
     "    uint64_t pixel_shader_hash;\n"
     "    uint32_t index_count;\n"
     "    uint32_t primitive_type;\n"
     "  };\n"
     "  struct DrawRecord {\n"
     "    uint64_t submission;\n"
     "    uint32_t op_index;\n"
     "    uint32_t index_count;\n"
     "    uint32_t primitive_type;\n"
     "    uint32_t pad;\n"
     "    uint64_t vertex_shader_hash;\n"
     "    uint64_t pixel_shader_hash;\n"
     "  };\n"
     "  static constexpr size_t kDrawRecordCount = 16384;\n"
     "  void TagNextDraw(const DrawTagInfo& tag) {\n"
     "    auto& args = *reinterpret_cast<DrawTagInfo*>(\n"
     "        WriteCommand(Command::kDrawTag, sizeof(DrawTagInfo)));\n"
     "    args = tag;\n"
     "  }\n"
     "  void SetExecuteSubmission(uint64_t submission) { execute_submission_ = submission; }\n"
     "  const std::array<DrawRecord, kDrawRecordCount>& draw_records() const { return draw_records_; }\n"
     "  uint32_t last_execute_op_count() const { return last_execute_op_count_; }\n"
     "\n"
     "  void EndDebugMarker() { WriteCommand(Command::kEndDebugMarker, 0); }\n"),
    ("  std::vector<uintmax_t> command_stream_;\n};\n",
     "  std::vector<uintmax_t> command_stream_;\n"
     "  // [dred] see DrawRecord\n"
     "  std::array<DrawRecord, kDrawRecordCount> draw_records_{};\n"
     "  size_t draw_record_next_ = 0;\n"
     "  uint32_t last_execute_op_count_ = 0;\n"
     "  uint64_t execute_submission_ = 0;\n"
     "};\n"),
])

C = "src/graphics/d3d12/deferred_command_list.cpp"
edit(C, [
    ("  ID3D12PipelineState* current_pipeline_state = nullptr;\n  while (stream_remaining != 0) {\n",
     "  ID3D12PipelineState* current_pipeline_state = nullptr;\n"
     "  uint32_t op_index = 0;  // [dred] the ops DRED's breadcrumbs count, in order\n"
     "  DrawTagInfo pending_tag{};\n"
     "  while (stream_remaining != 0) {\n"),
    # clears (3): disambiguated by the case that follows each
    ("            args.num_rects ? reinterpret_cast<const D3D12_RECT*>(&args + 1) : nullptr);\n"
     "      } break;\n      case Command::kD3DClearRenderTargetView: {\n",
     "            args.num_rects ? reinterpret_cast<const D3D12_RECT*>(&args + 1) : nullptr);\n"
     "        ++op_index;\n      } break;\n      case Command::kD3DClearRenderTargetView: {\n"),
    ("            args.num_rects ? reinterpret_cast<const D3D12_RECT*>(&args + 1) : nullptr);\n"
     "      } break;\n      case Command::kD3DClearUnorderedAccessViewUint: {\n",
     "            args.num_rects ? reinterpret_cast<const D3D12_RECT*>(&args + 1) : nullptr);\n"
     "        ++op_index;\n      } break;\n      case Command::kD3DClearUnorderedAccessViewUint: {\n"),
    ("            args.num_rects ? reinterpret_cast<const D3D12_RECT*>(&args + 1) : nullptr);\n"
     "      } break;\n      case Command::kD3DCopyBufferRegion: {\n",
     "            args.num_rects ? reinterpret_cast<const D3D12_RECT*>(&args + 1) : nullptr);\n"
     "        ++op_index;\n      } break;\n      case Command::kD3DCopyBufferRegion: {\n"),
    # copies
    ("                                       args.src_offset, args.num_bytes);\n      } break;\n",
     "                                       args.src_offset, args.num_bytes);\n        ++op_index;\n      } break;\n"),
    ("        command_list->CopyResource(args.dst_resource, args.src_resource);\n      } break;\n",
     "        command_list->CopyResource(args.dst_resource, args.src_resource);\n        ++op_index;\n      } break;\n"),
    ("        command_list->CopyTextureRegion(&args.dst, 0, 0, 0, &args.src, nullptr);\n      } break;\n",
     "        command_list->CopyTextureRegion(&args.dst, 0, 0, 0, &args.src, nullptr);\n        ++op_index;\n      } break;\n"),
    ("                                        args.has_src_box ? &args.src_box : nullptr);\n      } break;\n",
     "                                        args.has_src_box ? &args.src_box : nullptr);\n        ++op_index;\n      } break;\n"),
    # dispatch + draws (only when actually issued)
    ("          command_list->Dispatch(args.thread_group_count_x, args.thread_group_count_y,\n"
     "                                 args.thread_group_count_z);\n        }\n      } break;\n",
     "          command_list->Dispatch(args.thread_group_count_x, args.thread_group_count_y,\n"
     "                                 args.thread_group_count_z);\n          ++op_index;\n        }\n      } break;\n"),
    ("                                             args.start_instance_location);\n        }\n      } break;\n",
     "                                             args.start_instance_location);\n"
     "          auto& rec = draw_records_[draw_record_next_++ % kDrawRecordCount];\n"
     "          rec = {execute_submission_, op_index, pending_tag.index_count, pending_tag.primitive_type,\n"
     "                 0, pending_tag.vertex_shader_hash, pending_tag.pixel_shader_hash};\n"
     "          ++op_index;\n        }\n      } break;\n"),
    ("                                      args.start_vertex_location, args.start_instance_location);\n        }\n      } break;\n",
     "                                      args.start_vertex_location, args.start_instance_location);\n"
     "          auto& rec = draw_records_[draw_record_next_++ % kDrawRecordCount];\n"
     "          rec = {execute_submission_, op_index, pending_tag.index_count, pending_tag.primitive_type,\n"
     "                 0, pending_tag.vertex_shader_hash, pending_tag.pixel_shader_hash};\n"
     "          ++op_index;\n        }\n      } break;\n"),
    # resolve query data, barrier, markers
    ("                                       args.aligned_destination_buffer_offset);\n      } break;\n",
     "                                       args.aligned_destination_buffer_offset);\n        ++op_index;\n      } break;\n"),
    ("                rex::align(sizeof(UINT), alignof(D3D12_RESOURCE_BARRIER))));\n      } break;\n",
     "                rex::align(sizeof(UINT), alignof(D3D12_RESOURCE_BARRIER))));\n        ++op_index;\n      } break;\n"),
    ("        command_list->BeginEvent(1, label_name, static_cast<UINT>(args.label_length + 1));\n      } break;\n",
     "        command_list->BeginEvent(1, label_name, static_cast<UINT>(args.label_length + 1));\n        ++op_index;\n      } break;\n"),
    ("        command_list->EndEvent();\n      } break;\n",
     "        command_list->EndEvent();\n        ++op_index;\n      } break;\n"),
    ("        command_list->SetMarker(1, label_name, static_cast<UINT>(args.label_length + 1));\n      } break;\n"
     "      default:\n",
     "        command_list->SetMarker(1, label_name, static_cast<UINT>(args.label_length + 1));\n        ++op_index;\n      } break;\n"
     "      case Command::kDrawTag: {\n"
     "        pending_tag = *reinterpret_cast<const DrawTagInfo*>(stream);\n"
     "      } break;\n"
     "      default:\n"),
    ("    stream += header.arguments_size_elements;\n    stream_remaining -= header.arguments_size_elements;\n  }\n",
     "    stream += header.arguments_size_elements;\n    stream_remaining -= header.arguments_size_elements;\n  }\n"
     "  last_execute_op_count_ = op_index;\n"),
])

# --- command processor: tag the draws, mark the submission, name the hung draw --
P = "src/graphics/d3d12/command_processor.cpp"
edit(P, [
    ("    deferred_command_list_.Execute(command_list_, command_list_1_);\n",
     "    deferred_command_list_.SetExecuteSubmission(submission_current_);\n"
     "    deferred_command_list_.Execute(command_list_, command_list_1_);\n"),
    ("    deferred_command_list_.D3DDrawInstanced(primitive_processing_result.host_draw_vertex_count, 1,\n"
     "                                            0, 0);\n",
     "    deferred_command_list_.TagNextDraw(\n"
     "        {vertex_shader ? vertex_shader->ucode_data_hash() : 0ull,\n"
     "         pixel_shader ? pixel_shader->ucode_data_hash() : 0ull, index_count,\n"
     "         uint32_t(primitive_type)});\n"
     "    deferred_command_list_.D3DDrawInstanced(primitive_processing_result.host_draw_vertex_count, 1,\n"
     "                                            0, 0);\n"),
    ("    deferred_command_list_.D3DDrawIndexedInstanced(\n"
     "        primitive_processing_result.host_draw_vertex_count, 1, 0, 0, 0);\n",
     "    deferred_command_list_.TagNextDraw(\n"
     "        {vertex_shader ? vertex_shader->ucode_data_hash() : 0ull,\n"
     "         pixel_shader ? pixel_shader->ucode_data_hash() : 0ull, index_count,\n"
     "         uint32_t(primitive_type)});\n"
     "    deferred_command_list_.D3DDrawIndexedInstanced(\n"
     "        primitive_processing_result.host_draw_vertex_count, 1, 0, 0, 0);\n"),
    ("      for (uint32_t i = start; i < end; i++) {\n"
     "        REXGPU_ERROR(\"  [{}] op type {}{}\", i, static_cast<int>(node->pCommandHistory[i]),\n"
     "                     i == last ? \" <-- FAULT\" : \"\");\n"
     "      }\n",
     "      for (uint32_t i = start; i < end; i++) {\n"
     "        REXGPU_ERROR(\"  [{}] op type {}{}\", i, static_cast<int>(node->pCommandHistory[i]),\n"
     "                     i == last ? \" <-- FAULT\" : \"\");\n"
     "      }\n"
     "      // [dred] The draws recorded around that op, by the deferred list's own\n"
     "      // count of the same ops (DeferredCommandList::DrawRecord). The two most\n"
     "      // recent submissions are the candidates for the hung list.\n"
     "      {\n"
     "        const auto& recs = deferred_command_list_.draw_records();\n"
     "        const uint64_t newest = submission_current_;\n"
     "        REXGPU_ERROR(\"  ops in the last list by our count: {} (DRED: {})\",\n"
     "                     deferred_command_list_.last_execute_op_count(), node->BreadcrumbCount);\n"
     "        int printed = 0;\n"
     "        for (const auto& r : recs) {\n"
     "          if (r.submission == 0 || r.submission + 1 < newest) continue;\n"
     "          if (r.op_index + 2 < last || r.op_index > last + 2) continue;\n"
     "          REXGPU_ERROR(\"  draw at op {} (submission {}): vs {:016X} ps {:016X} indices {} prim {}{}\",\n"
     "                       r.op_index, r.submission, r.vertex_shader_hash, r.pixel_shader_hash,\n"
     "                       r.index_count, r.primitive_type,\n"
     "                       r.op_index == last ? \" <-- THE HUNG DRAW\" : \"\");\n"
     "          if (++printed >= 12) break;\n"
     "        }\n"
     "        if (!printed)\n"
     "          REXGPU_ERROR(\"  no draw record near op {} in the last two submissions\", last);\n"
     "      }\n"),
])
print("done")
