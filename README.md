# MacOS壁纸软件demo
**注意：** 本示例中的程序仅在m4芯片上进行过编译，MacOS26，其余设备待验证。


## 技术说明
### 一、程序设计结构
- 本软件分为两个主体程序构成：
    1. UI部分
    2. 渲染端
- UI部分便于用户进行更换，停止壁纸渲染等功能，demo中仅设计了壁纸预览（动态壁纸只预览一遍后不再循环播放），用户可打开目录选择使用的图片或视频作为壁纸，图片支持jpg/jpeg格式，视频支持mp4格式。
- 渲染端由用户点击设置壁纸后自动拉起，支持动态壁纸以及静态壁纸。
- UI端和渲染程序完全分离。

### 二、UI端技术说明
- UI窗口采用SDL3绘制，UI全程由CPU渲染，设有预览窗口，视频预览由ffmpeg进行解码，并转换为SDL的Texture渲染在窗口中，图片加载采用stb进行读取，数据加载以及解码运行在其他线程，与主画面完全异步执行。

### 三、渲染端技术说明
- 渲染端由MacOS的原生window进行（NSWindow），并且选择窗口为kCGDesktopWindowLevelKey，设置后可作为全屏背景，当前窗口仅用于主窗口。
- 图像渲染采用Metal进行渲染，原生GPU绘制，保证渲染效率。
- 视频解码采用VideoToolbox框架进行，采用硬解码，解码后的视频帧需要转换成metal支持的纹理即可进行渲染。顶点着色器采用全屏三角形，多出部分由GPU自动裁剪。
- 视频帧队列采用循环列表进行，解码线程解码出一帧放入队列，渲染线程取出帧进行转换和渲染，目前解码队列设置为3，解码线程解码完后发现队列已满渲染线程还没取走视频帧的时候进入循环等待，保证壁纸渲染不会出现撕裂等问题。
- 注意代码中需要使用c++桥接OC的代码，编译时必须要支持OC。

## 编译方法
1. 编译前准备
   - 下载SDL3，可选择brew安装或源码编译，本示例中SDL3采用源码编译的形式。
   - 使用brew安装ffmpeg
   - 下载metal-cpp
   - 安装xcode，一定要完整的，command tools缺少必要的工具。并且为编译着色器，还需要安装MetalToolchain。
   - 需要nlohmann的json库，head only，放到3rdparty中，另外如果需要调试需要下载spdlog写出日志进行调试。
```
    xcodebuild -downloadComponent MetalToolchain
```
   

1. clone本仓库代码
```
    git clone https://github.com/kyou2000/MacOsWallPaperDemo.git
```
3. 编译
```
    mkdir build
    cd build
    # 以下环境变量需要设置好
    export METALCPP_HOME=/your/path
    export SDL_HOME=/your/path
    export STB_PATH=/your/path
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make
```

4. cmake中已经设置程序打包为bundle，进入build中点击运行WallPaper.app即可。