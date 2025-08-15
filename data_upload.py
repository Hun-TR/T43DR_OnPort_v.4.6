#!/usr/bin/env python3
"""
TEİAŞ EKLİM - LittleFS Data Upload Script
Bu script data/ klasöründeki dosyaları ESP32'nin LittleFS sistemine yükler.

Kullanım:
    python data_upload.py
    veya
    pio run --target uploadfs
"""

import os
import sys
import subprocess
import shutil
from pathlib import Path

def check_platformio():
    """PlatformIO kurulu mu kontrol et"""
    try:
        result = subprocess.run(['pio', '--version'], capture_output=True, text=True)
        if result.returncode == 0:
            print(f"✅ PlatformIO bulundu: {result.stdout.strip()}")
            return True
        else:
            print("❌ PlatformIO bulunamadı!")
            return False
    except FileNotFoundError:
        print("❌ PlatformIO command line tool bulunamadı!")
        print("   Kurulum: pip install platformio")
        return False

def check_data_folder():
    """data/ klasörü var mı ve dosyalar var mı kontrol et"""
    data_dir = Path("data")
    
    if not data_dir.exists():
        print("❌ data/ klasörü bulunamadı!")
        return False, []
    
    files = list(data_dir.glob("*"))
    html_files = list(data_dir.glob("*.html"))
    css_files = list(data_dir.glob("*.css"))
    js_files = list(data_dir.glob("*.js"))
    
    print(f"📁 data/ klasörü bulundu")
    print(f"   📄 Toplam dosya: {len(files)}")
    print(f"   🌐 HTML dosyası: {len(html_files)}")
    print(f"   🎨 CSS dosyası: {len(css_files)}")
    print(f"   ⚡ JS dosyası: {len(js_files)}")
    
    # Eksik dosyaları kontrol et
    required_files = [
        "index.html", "login.html", "account.html", 
        "ntp.html", "baudrate.html", "fault.html", "log.html",
        "style.css", "script.js"
    ]
    
    missing_files = []
    for req_file in required_files:
        if not (data_dir / req_file).exists():
            missing_files.append(req_file)
    
    if missing_files:
        print(f"⚠️  Eksik dosyalar: {', '.join(missing_files)}")
        return True, missing_files
    else:
        print("✅ Tüm gerekli dosyalar mevcut")
        return True, []

def create_missing_files(missing_files):
    """Eksik dosyalar için placeholder oluştur"""
    data_dir = Path("data")
    
    for filename in missing_files:
        file_path = data_dir / filename
        
        if filename.endswith('.html'):
            content = f"""<!DOCTYPE html>
<html lang="tr">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{filename.replace('.html', '').title()} - TEİAŞ EKLİM</title>
    <link rel="stylesheet" href="style.css">
</head>
<body>
    <div class="container">
        <h1>📄 {filename}</h1>
        <p>Bu sayfa henüz tamamlanmamış.</p>
        <a href="/">Ana Sayfa</a>
    </div>
    <script src="script.js"></script>
</body>
</html>"""
        
        elif filename.endswith('.css'):
            content = """/* TEİAŞ EKLİM - Placeholder CSS */
:root {
    --primary: #667eea;
    --secondary: #764ba2;
    --bg-primary: #ffffff;
    --text-primary: #2d3748;
}

* {
    margin: 0;
    padding: 0;
    box-sizing: border-box;
}

body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
    background: var(--bg-primary);
    color: var(--text-primary);
}

.container {
    max-width: 1200px;
    margin: 0 auto;
    padding: 2rem;
}
"""
        
        elif filename.endswith('.js'):
            content = """// TEİAŞ EKLİM - Placeholder JavaScript
console.log('TEİAŞ EKLİM Script loaded');

document.addEventListener('DOMContentLoaded', function() {
    console.log('DOM Content Loaded');
    
    // Placeholder fonksiyonalite
    const title = document.querySelector('h1');
    if (title) {
        title.style.color = '#667eea';
    }
});
"""
        
        else:
            content = f"# {filename}\nPlaceholder content for {filename}"
        
        try:
            with open(file_path, 'w', encoding='utf-8') as f:
                f.write(content)
            print(f"✅ Placeholder oluşturuldu: {filename}")
        except Exception as e:
            print(f"❌ {filename} oluşturulamadı: {e}")

def get_file_sizes():
    """data/ klasöründeki dosya boyutlarını hesapla"""
    data_dir = Path("data")
    total_size = 0
    file_info = []
    
    for file_path in data_dir.glob("*"):
        if file_path.is_file():
            size = file_path.stat().st_size
            total_size += size
            file_info.append((file_path.name, size))
    
    return total_size, file_info

def upload_filesystem():
    """LittleFS'e dosyaları yükle"""
    print("\n🚀 LittleFS upload başlatılıyor...")
    
    try:
        # PlatformIO uploadfs komutu
        result = subprocess.run(
            ['pio', 'run', '--target', 'uploadfs'], 
            capture_output=True, 
            text=True, 
            timeout=300  # 5 dakika timeout
        )
        
        if result.returncode == 0:
            print("✅ LittleFS upload başarılı!")
            print("\n📤 Upload çıktısı:")
            print(result.stdout)
            return True
        else:
            print("❌ LittleFS upload başarısız!")
            print("\n📤 Upload hatası:")
            print(result.stderr)
            return False
            
    except subprocess.TimeoutExpired:
        print("❌ Upload timeout! (5 dakika)")
        return False
    except Exception as e:
        print(f"❌ Upload hatası: {e}")
        return False

def verify_upload():
    """Upload sonrası doğrulama"""
    print("\n🔍 Upload doğrulaması...")
    
    # ESP32'ye bağlı mı kontrol et
    try:
        result = subprocess.run(['pio', 'device', 'list'], capture_output=True, text=True)
        if "ESP32" in result.stdout or "USB" in result.stdout:
            print("✅ ESP32 cihazı algılandı")
            return True
        else:
            print("⚠️  ESP32 cihazı algılanamadı, upload doğrulanamıyor")
            return False
    except Exception as e:
        print(f"⚠️  Cihaz kontrolü yapılamadı: {e}")
        return False

def main():
    """Ana fonksiyon"""
    print("🔧 TEİAŞ EKLİM - LittleFS Data Upload Tool")
    print("=" * 50)
    
    # PlatformIO kontrolü
    if not check_platformio():
        sys.exit(1)
    
    # data/ klasörü kontrolü
    data_exists, missing_files = check_data_folder()
    if not data_exists:
        sys.exit(1)
    
    # Eksik dosyalar varsa oluştur
    if missing_files:
        response = input(f"\n❓ {len(missing_files)} eksik dosya için placeholder oluşturulsun mu? (y/N): ")
        if response.lower() in ['y', 'yes', 'evet', 'e']:
            create_missing_files(missing_files)
        else:
            print("⚠️  Eksik dosyalar olmadan devam ediliyor...")
    
    # Dosya boyutları
    total_size, file_info = get_file_sizes()
    print(f"\n📊 Dosya boyutları:")
    for filename, size in sorted(file_info, key=lambda x: x[1], reverse=True):
        size_kb = size / 1024
        print(f"   📄 {filename:<15} {size_kb:>6.1f} KB")
    
    print(f"\n📦 Toplam boyut: {total_size / 1024:.1f} KB")
    
    # LittleFS kapasitesi (yaklaşık 1.5MB)
    littlefs_capacity = 1536 * 1024  # 1.5MB
    usage_percent = (total_size / littlefs_capacity) * 100
    
    print(f"💾 LittleFS kullanımı: {usage_percent:.1f}%")
    
    if usage_percent > 90:
        print("⚠️  LittleFS neredeyse dolu! Bazı dosyaları küçültmeyi düşünün.")
    elif usage_percent > 70:
        print("⚠️  LittleFS %70'den fazla dolu.")
    
    # Upload onayı
    print(f"\n🚀 {len(file_info)} dosya ESP32'ye yüklenecek.")
    response = input("❓ Upload işlemini başlat? (Y/n): ")
    
    if response.lower() not in ['', 'y', 'yes', 'evet', 'e']:
        print("❌ Upload iptal edildi.")
        sys.exit(0)
    
    # Upload işlemi
    success = upload_filesystem()
    
    if success:
        verify_upload()
        print("\n🎉 İşlem tamamlandı!")
        print("\n📋 Sonraki adımlar:")
        print("   1. ESP32'yi yeniden başlatın")
        print("   2. Web arayüzüne bağlanın")
        print("   3. Dosyaların doğru yüklendiğini kontrol edin")
        
        # Web arayüz URL'leri
        print("\n🌐 Web arayüz adresleri:")
        print("   • http://192.168.1.160 (varsayılan IP)")
        print("   • http://teias-xxxx.local (mDNS)")
        
    else:
        print("\n❌ Upload başarısız!")
        print("\n🔧 Sorun giderme önerileri:")
        print("   1. ESP32'nin USB kablosu bağlı mı?")
        print("   2. Doğru COM port seçili mi?")
        print("   3. ESP32 başka bir program tarafından kullanılıyor mu?")
        print("   4. PlatformIO güncel mi? (pio upgrade)")
        sys.exit(1)

if __name__ == "__main__":
    main()