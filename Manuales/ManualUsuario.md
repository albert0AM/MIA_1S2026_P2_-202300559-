# Manual de Usuario — MIA Proyecto 2
**Estudiante:** Douglas Alberto Ajú Mucía
**Carnet:** 202300559

---

## 1. ¿Qué es este sistema?

Es un simulador de sistema de archivos. Permite crear discos virtuales, particionarlos, formatearlos y manejar archivos y directorios, todo desde una interfaz web.

---

## 2. Acceso al Sistema

Abre el navegador y ve a:
```
http://mia-p2-frontend.s3-website.us-east-2.amazonaws.com
```

La interfaz tiene dos paneles:
- **Panel izquierdo:** Editor donde escribes los comandos.
- **Panel derecho:** Área de salida donde aparecen los resultados.

Para ejecutar comandos, escríbelos en el editor y presiona el botón **Ejecutar**.

> Las líneas que empiezan con `#` son comentarios y se ignoran.

---

## 3. Comandos Disponibles

### MKDISK — Crear disco
Crea un archivo que representa un disco virtual.

```
mkdisk -size=<número> -unit=<K|M> -path=<ruta> [-fit=<FF|BF|WF>]
```

| Parámetro | Descripción |
|-----------|-------------|
| -size | Tamaño del disco |
| -unit | Unidad: K (kilobytes) o M (megabytes) |
| -path | Ruta donde se guardará el archivo |
| -fit | Ajuste: FF (First Fit), BF (Best Fit), WF (Worst Fit). Default: FF |

**Ejemplo:**
```
mkdisk -size=50 -unit=M -path=/home/ubuntu/Discos/disco1.mia
```

---

### RMDISK — Eliminar disco
Elimina un disco virtual.
```
rmdisk -path=<ruta>
```

---

### FDISK — Particionar disco
Crea, elimina o modifica particiones en un disco.

**Crear partición:**
```
fdisk -type=<P|E|L> -name=<nombre> -size=<número> -unit=<B|K|M> -path=<ruta> [-fit=<FF|BF|WF>]
```

| Parámetro | Descripción |
|-----------|-------------|
| -type | Tipo: P (Primaria), E (Extendida), L (Lógica) |
| -name | Nombre de la partición (máx. 16 caracteres) |
| -size | Tamaño |
| -unit | Unidad: B (bytes), K (kilobytes), M (megabytes) |
| -path | Ruta del disco |

**Eliminar partición:**
```
fdisk -delete=<fast|full> -name=<nombre> -path=<ruta>
```

**Modificar tamaño:**
```
fdisk -add=<bytes> -name=<nombre> -path=<ruta>
```
> El valor de `-add` es en bytes. Usa número negativo para reducir.

**Ejemplos:**
```
fdisk -type=P -name=Particion1 -size=20 -unit=M -path=/home/ubuntu/Discos/disco1.mia -fit=BF
fdisk -delete=fast -name=Particion1 -path=/home/ubuntu/Discos/disco1.mia
fdisk -add=-200 -name=Particion1 -path=/home/ubuntu/Discos/disco1.mia
```

---

### MOUNT — Montar partición
Monta una partición para poder usarla. Al montar, el sistema asigna un **ID único**.

```
mount -path=<ruta_disco> -name=<nombre_partición>
```

**Ejemplo:**
```
mount -path=/home/ubuntu/Discos/disco1.mia -name=Particion1
```
**Salida:**
```
SUCCESS: partición montada
  nombre : Particion1
  id     : 591A
  disco  : /home/ubuntu/Discos/disco1.mia
  letra  : A
```
> El **ID** (ej. `591A`) se usa en todos los comandos siguientes.

---

### MOUNTED — Listar particiones montadas
```
mounted
```
Muestra todas las particiones actualmente montadas con sus IDs.

---

### MKFS — Formatear partición
Formatea una partición montada con sistema de archivos EXT2.
```
mkfs -id=<ID> [-type=full]
```
**Ejemplo:**
```
mkfs -id=591A -type=full
```

---

### LOGIN — Iniciar sesión
```
login -user=<usuario> -pass=<contraseña> -id=<ID>
```
**Ejemplo:**
```
login -user=root -pass=123 -id=591A
```
> Solo puede haber una sesión activa a la vez.

---

### LOGOUT — Cerrar sesión
```
logout
```

---

### MKGRP — Crear grupo
```
mkgrp -name=<nombre>
```
> Requiere sesión activa como root.

---

### RMGRP — Eliminar grupo
```
rmgrp -name=<nombre>
```

---

### MKUSR — Crear usuario
```
mkusr -user=<usuario> -pass=<contraseña> -grp=<grupo>
```

---

### RMUSR — Eliminar usuario
```
rmusr -user=<usuario>
```

---

### CHGRP — Cambiar grupo de usuario
```
chgrp -user=<usuario> -grp=<nuevo_grupo>
```

---

### MKDIR — Crear directorio
```
mkdir -path=<ruta> [-p]
```
| Parámetro | Descripción |
|-----------|-------------|
| -path | Ruta del directorio |
| -p | Crea los directorios padres si no existen |

**Ejemplos:**
```
mkdir -path=/home/archivos
mkdir -p -path=/home/archivos/user/docs/usac
```

---

### MKFILE — Crear archivo
```
mkfile -path=<ruta> [-size=<número>] [-cont=<ruta_contenido>] [-r]
```

| Parámetro | Descripción |
|-----------|-------------|
| -path | Ruta del archivo a crear |
| -size | Tamaño en bytes (se rellena con números del 0-9) |
| -cont | Ruta de un archivo local cuyo contenido se copiará |
| -r | Crea los directorios padres si no existen |

**Ejemplos:**
```
mkfile -path=/home/docs/tarea.txt -size=100
mkfile -path=/home/docs/tarea.txt -cont=/home/ubuntu/CONT/NAME.txt
```

---

### CAT — Ver contenido de archivo
```
cat -file1=<ruta>
```
**Ejemplo:**
```
cat -file1=/users.txt
```

---

### REP — Generar reporte
```
rep -id=<ID> -path=<ruta_salida> -name=<tipo_reporte> [-path_file_ls=<ruta>]
```

| Tipo | Descripción |
|------|-------------|
| disk | Visualización del disco y sus particiones |
| mbr | Tabla del MBR |
| inode | Tabla de inodos |
| block | Tabla de bloques |
| bm_inode | Bitmap de inodos (archivo .txt) |
| bm_block | Bitmap de bloques (archivo .txt) |
| sb | Superbloque |
| file | Contenido de un archivo específico |
| ls | Listado de un directorio |
| tree | Árbol de archivos del sistema |

**Ejemplos:**
```
rep -id=591A -path=/home/ubuntu/Reportes/disco.jpg -name=disk
rep -id=591A -path=/home/ubuntu/Reportes/arbol.png -name=tree
```

---

## 4. Flujo Típico de Uso

```
# 1. Crear disco
mkdisk -size=50 -unit=M -path=/home/ubuntu/Discos/disco1.mia

# 2. Crear partición
fdisk -type=P -name=Part1 -size=40 -unit=M -path=/home/ubuntu/Discos/disco1.mia -fit=BF

# 3. Montar partición
mount -path=/home/ubuntu/Discos/disco1.mia -name=Part1

# 4. Formatear (usa el ID que devolvió mount, ej. 591A)
mkfs -id=591A -type=full

# 5. Iniciar sesión
login -user=root -pass=123 -id=591A

# 6. Crear directorios y archivos
mkdir -p -path=/home/usuario/documentos
mkfile -path=/home/usuario/documentos/nota.txt -size=50

# 7. Ver archivo
cat -file1=/home/usuario/documentos/nota.txt

# 8. Generar reporte
rep -id=591A -path=/home/ubuntu/Reportes/tree.png -name=tree

# 9. Cerrar sesión
logout
```

---

## 5. Errores Comunes

| Error | Causa | Solución |
|-------|-------|----------|
| `ERROR: no hay espacio suficiente` | El disco está lleno | Usa un disco más grande o elimina particiones |
| `ERROR: falta -name` | Olvidaste el parámetro -name en fdisk | Agrega `-name=<nombre>` |
| `ERROR: no se encontró la partición` | La partición no existe con ese nombre | Verifica el nombre con `mounted` |
| `ERROR: sesión no iniciada` | Intentas un comando que requiere login | Ejecuta `login` primero |
| `ERROR: no se pudo conectar con el servidor` | El backend no responde | Verifica que el EC2 esté corriendo |
