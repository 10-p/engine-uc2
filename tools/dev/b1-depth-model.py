#!/usr/bin/env python3
"""CPU model of the depth of one point drawn by two passes (projected shadows over terrain).

The terrain is drawn by GL's fixed-function vertex stage: modelview and projection are loaded separately
(FOpenGLRenderInterface::CommitMatrices), the GPU computes eye = MV v, clip = P eye. The projector batch drawn
over it (ProjectorAtten.vsh) gets u_clip = MV x P multiplied on the CPU (UC2GLShaders.cpp, GetGLMatrices) and
computes clip = u_clip v. Same vertex, same matrices, two float32 roundings. This script emulates both in float32
(every product and sum rounded, no fused multiply-add) for a camera on CTF-Aqueduct and prints, per screen
row of a plane under the camera (a ramp), the depth difference of the two paths in 24-bit depth units, against:
the port's polygon offset (5 units, no slope), the Xbox's ZBIAS 1 (slope scale 0.25 + 1 unit; XDK d3d8.lib
D3DDevice_SetRenderState_ZBias) and UT2004's OpenGL driver (slope 1 + 1 unit).

  b1-depth-model.py [--yaw-steps N]   (also turns the camera by 8 yaw units N times: the error changes sign)

Assumptions (marked as such in the notebook): FOV 90 (85 in the default ini changes nothing material), eye 60
units above the pawn's location, 1280x960, a plane rising 12 degrees along the view at the pawn's feet. The GPU may
round differently (fused operations); the model gives the size of the difference, not its exact value.
"""
import math
import struct
import argparse


def f(x):
    return struct.unpack('f', struct.pack('f', x))[0]


def mat_mul(a, b):  # row-vector convention, as FMatrix: (a*b)[i][j] = sum_k a[i][k] b[k][j], float32
    r = [[0.0] * 4 for _ in range(4)]
    for i in range(4):
        for j in range(4):
            s = 0.0
            for k in range(4):
                s = f(s + f(a[i][k] * b[k][j]))
            r[i][j] = s
    return r


def vec_mul(v, m):  # v * M, float32
    return [f(f(f(f(v[0] * m[0][j]) + f(v[1] * m[1][j])) + f(v[2] * m[2][j])) + f(v[3] * m[3][j])) for j in range(4)]


def depth_units(clip):
    z = f(clip[2] / clip[3])
    return f(f(z * 0.5) + 0.5) * (1 << 24)


def camera(cx, cy, cz, yaw, pitch, W, H, fov_deg):
    Y = yaw * 2 * math.pi / 65536
    P = pitch * 2 * math.pi / 65536
    fwd = (math.cos(P) * math.cos(Y), math.cos(P) * math.sin(Y), math.sin(P))
    right = (-math.sin(Y), math.cos(Y), 0.0)
    up = (-math.sin(P) * math.cos(Y), -math.sin(P) * math.sin(Y), math.cos(P))
    T = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [f(-cx), f(-cy), f(-cz), 1]]
    # inverse rotation + UE2's camera axes (x right, y down, z forward), as one matrix
    R = [[f(right[0]), f(-up[0]), f(fwd[0]), 0], [f(right[1]), f(-up[1]), f(fwd[1]), 0],
         [f(right[2]), f(-up[2]), f(fwd[2]), 0], [0, 0, 0, 1]]
    MV = mat_mul(T, R)
    n, far = 10.0, 65536.0
    t = math.tan(math.radians(fov_deg) / 2)
    Pm = [[f(1 / t), 0, 0, 0], [0, f(W / H / t), 0, 0], [0, 0, f(far / (far - n)), 1], [0, 0, f(-n * far / (far - n)), 0]]
    Fix = [[1, 0, 0, 0], [0, -1, 0, 0], [0, 0, 2, 0], [f(1 / W), f(1 / H), -1, 1]]
    Proj = mat_mul(Pm, Fix)
    return MV, Proj, mat_mul(MV, Proj), (fwd, right, up)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--yaw-steps', type=int, default=4)
    a = ap.parse_args()
    W, H, fov = 1280, 960, 90.0
    px, py, pz, yaw0, pitch = 5284.9, 858.1, -104.6, 30811, 418
    eye = (px, py, pz + 60.0)
    t = math.tan(math.radians(fov) / 2)
    print('row  dist  |A-B| max/mean (depth units)  slope/pixel  port(0,5)  xbox(0.25,1)  ut2004(1,1)   [yaw %d + 8k]' % yaw0)
    for k in range(a.yaw_steps + 1):
        MV, Proj, Clip, (fwd, right, up) = camera(*eye, yaw0 + 8 * k, pitch, W, H, fov)
        # the ramp: a plane through the feet, rising 12 degrees along the horizontal view direction
        hf = (fwd[0], fwd[1], 0.0)
        hl = math.hypot(hf[0], hf[1])
        hf = (hf[0] / hl, hf[1] / hl, 0.0)
        s = math.radians(12)
        nrm = (-hf[0] * math.sin(s), -hf[1] * math.sin(s), math.cos(s))
        feet = (px, py, pz - 40.0)
        if k:
            print('-- yaw +%d' % (8 * k))
        for row in (500, 560, 620, 700, 800, 900):
            diffs = []
            slopes = []
            for col in range(300, 1000, 7):
                def hit(c, r):
                    sx = (2 * (c + 0.5) / W - 1) * t
                    sy = (1 - 2 * (r + 0.5) / H) * t * H / W
                    d = [fwd[i] + sx * right[i] + sy * up[i] for i in range(3)]
                    den = sum(d[i] * nrm[i] for i in range(3))
                    lam = sum((feet[i] - eye[i]) * nrm[i] for i in range(3)) / den
                    return [eye[i] + lam * d[i] for i in range(3)], lam
                p, lam = hit(col, row)
                if lam <= 0:
                    continue
                v = [f(p[0]), f(p[1]), f(p[2]), 1.0]
                A = vec_mul(vec_mul(v, MV), Proj)      # fixed function: P (MV v)
                B = vec_mul(v, Clip)                   # our vertex shader: (MV P) v
                diffs.append(abs(depth_units(A) - depth_units(B)))
                # exact depth slope per pixel along the column (double precision)
                def exact(c, r):
                    q, _ = hit(c, r)
                    z = sum((q[i] - eye[i]) * fwd[i] for i in range(3))
                    return (1 - 10.0 / z) * 65536.0 / (65536.0 - 10.0) * (1 << 24)  # window depth, ~ (1 - n/z)
                slopes.append(max(abs(exact(col, row + 1) - exact(col, row)), abs(exact(col + 1, row) - exact(col, row))))
            if not diffs:
                continue
            m = sum(slopes) / len(slopes)
            print('%4d %5.0f  %8.1f / %6.1f            %10.1f  %9.1f  %12.1f  %11.1f' % (
                row, lam, max(diffs), sum(diffs) / len(diffs), m, 5.0, 0.25 * m + 1, m + 1))


if __name__ == '__main__':
    main()
