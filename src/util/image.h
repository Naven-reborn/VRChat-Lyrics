#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace util {

// Decodes a JPEG/PNG/BMP byte buffer via WIC, applies a circular alpha mask
// so the rendered quad looks like a CD disc, and uploads to D3D11 as RGBA.
// Returns nullptr on failure. Caller releases via SRV->Release().
ID3D11ShaderResourceView* CreateCircularTexture(ID3D11Device* device,
                                                 const uint8_t* data,
                                                 size_t size,
                                                 int target_px = 128);

}
