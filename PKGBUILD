# Maintainer: nagnagi
pkgname=hyprtab
pkgver=1.0.0
pkgrel=1
pkgdesc='Alt-Tab style window switcher for Hyprland (Qt6/QML overlay)'
arch=('x86_64')
url='https://github.com/nagnagi/hyprtab'
license=('MIT')
depends=(
    'hyprland'
    'qt6-declarative'
)
makedepends=(
    'cmake'
    'git'
    'qt6-base'
)
source=("git+ssh://git@github.com/nagnagi/hyprtab.git#branch=main")
sha256sums=('SKIP')

build() {
    cmake -S "$srcdir/$pkgname" -B "$srcdir/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build "$srcdir/build" -j"$(nproc)"
}

package() {
    install -Dm755 "$srcdir/build/hyprtab" "$pkgdir/usr/bin/hyprtab"
    install -Dm644 "$srcdir/$pkgname/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
