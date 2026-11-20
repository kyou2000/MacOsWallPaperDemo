
# MSL着色器编译方式
```
    xcrun -sdk macosx metal -c shaders.metal -o shaders.air
    xcrun -sdk macosx metal shaders.air -o shaders.metallib
```