set -euxo pipefail
echo "making SHV ${WORKSPACE:-}"
export PATH=/c/3dParty/mingw/x86_64-13.1.0-release-posix-seh-msvcrt-rt_v11-rev1/mingw64/bin:/c/cmake-3.29.0-rc4-windows-x86_64/bin:/c/Qt6/6.8.1/mingw_64/bin:$PATH
export CXXFLAGS="-DGIT_COMMIT=${CI_COMMIT_SHA} -DGIT_BRANCH=${CI_COMMIT_REF_SLUG} -DBUILD_ID=${CI_PIPELINE_ID}"

cmake.exe \
    -DLIBSHV_WITH_WEBSOCKETS=OFF \
    -G "MinGW Makefiles" \
    -DCMAKE_PREFIX_PATH=C:/Qt6/6.8.1/mingw_64 \
    -DCMAKE_INSTALL_PREFIX=. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DUSE_QT6=ON \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache.exe \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache.exe \
    .

cmake.exe --build . -j8

# We're installing into the build directory, all the .iss scripts depend on that.
cmake.exe --install .

echo "making jn50view-doc"
pushd clients/jn50view/doc/help
c:/Qt/Tools/mingw730_64/bin/mingw32-make.exe html
pushd

# TODO: Reenable these, if we're ever going to install this somewhere. Also port them to windeployqt!
JN50VIEW_VERSION=`grep APP_VERSION clients/jn50view/src/version.h | cut -d\" -f2`
BFSVIEW_VERSION=`grep APP_VERSION clients/bfsview/src/version.h | cut -d\" -f2`

"C:\Program Files (x86)\Inno Setup 5\iscc.exe" "-DVERSION=${BFSVIEW_VERSION}" clients/bfsview/bfsview.iss || exit /b 2
"C:\Program Files (x86)\Inno Setup 5\iscc.exe" "-DVERSION=${JN50VIEW_VERSION}" clients/jn50view/jn50view.iss || exit /b 2

