from setuptools import setup, Extension, find_namespace_packages
from setuptools.command.build_ext import build_ext
import os
import subprocess
import multiprocessing
import shutil

class CMakeExtension(Extension):
    def __init__(self, name):
        super().__init__(name, sources=[])

def find_linked_python_lib(path):
    # Run ldd on the built library to find linked libraries
    ldd_output = subprocess.check_output(['ldd', f"{path}/quicsend_library.so"]).decode('utf-8')

    print(f"ldd_output = {ldd_output}")

    # Search for libpython in the output
    for line in ldd_output.split('\n'):
        if 'libpython3' in line:
            # Extract the path to the library
            return line.split(' => ')[1].split(' ')[0]
    
    return None

class BuildPackage(build_ext):
    def run(self):
        for ext in self.extensions:
            self.build_extension(ext)

    def build_extension(self, ext):
        # Ensure we have Rust installed
        try:
            subprocess.check_call(['rustc', '--version'])
        except:
            raise RuntimeError("Rust must be installed to build quiche")

        script_dir = os.path.dirname(os.path.abspath(__file__))

        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
        cmake_args = [
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={extdir}",
        ]
        build_args = ["--config", "Release"]

        num_threads = multiprocessing.cpu_count() - 1
        if num_threads > 1:
            build_args.append(f"-j{num_threads}")

        os.makedirs(self.build_temp, exist_ok=True)
        subprocess.check_call(["cmake", "-S", ".", "-B", self.build_temp] + cmake_args, cwd=script_dir)
        subprocess.check_call(["cmake", "--build", self.build_temp] + build_args, cwd=script_dir)

        lib_file = find_linked_python_lib(extdir)

        print(f"Linked Python library: {lib_file}")

        if lib_file:
            dst = os.path.join(self.build_lib, 'quicsend.libs')
            print(f"Copying to: {dst}")

            os.makedirs(dst, exist_ok=True)
            shutil.copy(lib_file, dst)

setup(
    name="quicsend",
    version="0.13.0",
    url="https://github.com/catid/quicsend",
    author="Chris Taylor",
    python_requires='>=3',
    ext_modules=[CMakeExtension("quicsend")],
    cmdclass={"build_ext": BuildPackage},
    package_data={
        'quicsend': ['quicsend_library.so', 'libpython3*.so*'],
    },
    include_package_data=True,
    long_description=open("DESCRIPTION.md").read(),
    long_description_content_type='text/markdown',
    package_dir={'': 'python_src'},
    packages=find_namespace_packages(where='python_src'),
    install_requires=['msgpack']
)
