package android.opengl;

/** Column-major 4x4 matrix helpers, as in android.opengl.Matrix. */
public class Matrix {
    public static void setIdentityM(float[] m, int o) {
        for (int i = 0; i < 16; i++) m[o + i] = 0;
        for (int i = 0; i < 16; i += 5) m[o + i] = 1.0f;
    }

    public static void multiplyMM(float[] r, int ro, float[] lhs, int lo, float[] rhs, int rho) {
        float[] t = new float[16];
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += lhs[lo + k * 4 + j] * rhs[rho + i * 4 + k];
                t[i * 4 + j] = s;
            }
        }
        for (int i = 0; i < 16; i++) r[ro + i] = t[i];
    }

    public static void orthoM(float[] m, int o, float left, float right, float bottom, float top, float near, float far) {
        float rw = 1.0f / (right - left), rh = 1.0f / (top - bottom), rd = 1.0f / (far - near);
        for (int i = 0; i < 16; i++) m[o + i] = 0;
        m[o] = 2.0f * rw;
        m[o + 5] = 2.0f * rh;
        m[o + 10] = -2.0f * rd;
        m[o + 12] = -(right + left) * rw;
        m[o + 13] = -(top + bottom) * rh;
        m[o + 14] = -(far + near) * rd;
        m[o + 15] = 1.0f;
    }

    public static void frustumM(float[] m, int o, float left, float right, float bottom, float top, float near, float far) {
        float rw = 1.0f / (right - left), rh = 1.0f / (top - bottom), rd = 1.0f / (near - far);
        for (int i = 0; i < 16; i++) m[o + i] = 0;
        m[o] = 2.0f * near * rw;
        m[o + 5] = 2.0f * near * rh;
        m[o + 8] = (right + left) * rw;
        m[o + 9] = (top + bottom) * rh;
        m[o + 10] = (far + near) * rd;
        m[o + 11] = -1.0f;
        m[o + 14] = 2.0f * far * near * rd;
    }

    public static void perspectiveM(float[] m, int o, float fovy, float aspect, float zNear, float zFar) {
        float f = 1.0f / (float) Math.tan(fovy * (Math.PI / 360.0));
        float rd = 1.0f / (zNear - zFar);
        for (int i = 0; i < 16; i++) m[o + i] = 0;
        m[o] = f / aspect;
        m[o + 5] = f;
        m[o + 10] = (zFar + zNear) * rd;
        m[o + 11] = -1.0f;
        m[o + 14] = 2.0f * zFar * zNear * rd;
    }

    public static void translateM(float[] m, int o, float x, float y, float z) {
        for (int i = 0; i < 4; i++) m[o + 12 + i] += m[o + i] * x + m[o + 4 + i] * y + m[o + 8 + i] * z;
    }

    public static void scaleM(float[] m, int o, float x, float y, float z) {
        for (int i = 0; i < 4; i++) { m[o + i] *= x; m[o + 4 + i] *= y; m[o + 8 + i] *= z; }
    }

    public static void rotateM(float[] m, int o, float a, float x, float y, float z) {
        float[] r = new float[16];
        setRotateM(r, 0, a, x, y, z);
        float[] t = new float[16];
        multiplyMM(t, 0, m, o, r, 0);
        for (int i = 0; i < 16; i++) m[o + i] = t[i];
    }

    public static void setRotateM(float[] m, int o, float a, float x, float y, float z) {
        m[o + 3] = 0; m[o + 7] = 0; m[o + 11] = 0; m[o + 12] = 0; m[o + 13] = 0; m[o + 14] = 0; m[o + 15] = 1;
        a *= (float) (Math.PI / 180.0);
        float s = (float) Math.sin(a), c = (float) Math.cos(a);
        float len = (float) Math.sqrt(x * x + y * y + z * z);
        if (len != 1.0f) { float rl = 1.0f / len; x *= rl; y *= rl; z *= rl; }
        float nc = 1.0f - c;
        m[o] = x * x * nc + c;
        m[o + 4] = x * y * nc - z * s;
        m[o + 8] = x * z * nc + y * s;
        m[o + 1] = y * x * nc + z * s;
        m[o + 5] = y * y * nc + c;
        m[o + 9] = y * z * nc - x * s;
        m[o + 2] = x * z * nc - y * s;
        m[o + 6] = y * z * nc + x * s;
        m[o + 10] = z * z * nc + c;
    }

    public static void setLookAtM(float[] m, int o, float ex, float ey, float ez, float cx, float cy, float cz, float ux, float uy, float uz) {
        float fx = cx - ex, fy = cy - ey, fz = cz - ez;
        float rlf = 1.0f / (float) Math.sqrt(fx * fx + fy * fy + fz * fz);
        fx *= rlf; fy *= rlf; fz *= rlf;
        float sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux;
        float rls = 1.0f / (float) Math.sqrt(sx * sx + sy * sy + sz * sz);
        sx *= rls; sy *= rls; sz *= rls;
        float tx = sy * fz - sz * fy, ty = sz * fx - sx * fz, tz = sx * fy - sy * fx;
        m[o] = sx; m[o + 1] = tx; m[o + 2] = -fx; m[o + 3] = 0;
        m[o + 4] = sy; m[o + 5] = ty; m[o + 6] = -fy; m[o + 7] = 0;
        m[o + 8] = sz; m[o + 9] = tz; m[o + 10] = -fz; m[o + 11] = 0;
        m[o + 12] = 0; m[o + 13] = 0; m[o + 14] = 0; m[o + 15] = 1;
        translateM(m, o, -ex, -ey, -ez);
    }
}
