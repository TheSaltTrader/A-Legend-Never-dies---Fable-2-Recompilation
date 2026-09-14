"""s79 - the 3D-rendered HUD widgets (the d-pad prompt with its shaded
buttons, and the skinned variant) are not drawn with c8: they carry a
perspective projection at c0..c3 with a HARD-CODED 16:9 aspect (the census
read c0 = (6.303, 0, 0, 0), c1 = (0, 0, 11.2, 0): 11.2 / 6.303 = 1.777),
depth test off, while the world's projection follows the display aspect. At
ultrawide those widgets stretched with the presenter. While fable2_uw_2d_k is
active, every depth-off draw whose vertex shader reads c0..c3 with c0 =
(sx, 0, 0, 0) and a single non-zero y scale in c1 within 1.5% of 16/9 times
sx gets c0.x multiplied by k: the widget lands in the centred 16:9 band at
its true proportions. Same register-file-read / upload-buffer-write
discipline as the c8 case (s78b). APPLY ONCE (requires patch_uw_2d_hud.py)."""
Q = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
q = open(Q, encoding="utf-8").read()
assert "fable2_uw_2d_k" in q, "needs patch_uw_2d_hud.py first"
assert "[uw-2d] perspective HUD" not in q, "already applied"

def rep(old, new):
    global q
    assert q.count(old) == 1, ("expected 1, found %d: %s" % (q.count(old), old[:90]))
    q = q.replace(old, new)

rep('''            out[0] = c8[0] * k;  // x scale
            out[2] = c8[2] * k;  // x offset (-1 -> -k keeps the band centred)
          }
        }
      }
    }
''', '''            out[0] = c8[0] * k;  // x scale
            out[2] = c8[2] * k;  // x offset (-1 -> -k keeps the band centred)
          }
        }
        // [uw-2d] perspective HUD widgets: a 16:9 projection at c0..c3 with
        // the depth test off (the d-pad prompt's shaded buttons). The world's
        // projection carries the display aspect, so it never matches here.
        if ((bmu[0] & 0xFull) == 0xFull) {
          float c0[8];
          std::memcpy(c0, &register_file_->values[XE_GPU_REG_SHADER_CONSTANT_000_X], sizeof(c0));
          const float sx = c0[0];
          if (sx > 1e-3f && c0[1] == 0.0f && c0[2] == 0.0f && c0[3] == 0.0f) {
            // c1 holds the y scale in exactly one lane.
            float sy = 0.0f;
            int lanes = 0;
            for (int i = 4; i < 8; ++i) {
              if (c0[i] != 0.0f) { sy = std::fabs(c0[i]); ++lanes; }
            }
            const float ratio = lanes == 1 ? sy / sx : 0.0f;
            if (ratio > 1.751f && ratio < 1.805f &&
                !draw_util::GetNormalizedDepthControl(*register_file_).z_enable) {
              // c0 is the first used register: packed position 0.
              float* out = reinterpret_cast<float*>(const_cast<uint8_t*>(float_constants_begin));
              out[0] = sx * static_cast<float>(kd);
            }
          }
        }
      }
    }
''')
open(Q, "w", encoding="utf-8", newline="").write(q)
print("patched: [uw-2d] perspective HUD widgets scaled by fable2_uw_2d_k")
