__winc_id__ = 'ae539110d03e49ea8738fd413ac44ba8'
__human_name__ = 'files'
from pathlib import Path
import os
import shutil 
import zipfile
path = os.getcwd()
cache_path_files = path + '\\cache'


# step 1
# clean_cache: takes no arguments and creates an empty folder named cache in the current directory. 
# If it already exists, it deletes everything in the cache folder.

def clean_cache():
  #print(path)
  list_directories  = os.listdir()
  # print(list_directories)
  if 'cache' in list_directories:
    #path= os.getcwd()
    #new_path = os.getcwd() + '\\cache'
    #[f.unlink() for f in Path(new_path).glob("*") if f.is_file()] 
    #os.rmdir('cache')
    
    shutil.rmtree('cache', ignore_errors=True)
    os.mkdir('cache')
  else:
    os.mkdir('cache')

  print(os.listdir())

# clean_cache()

# step 2
# cache_zip: takes a zip file path (str) and a cache dir path (str) as arguments, in that order. 
# The function then unpacks the indicated zip file into a clean cache folder.
# You can test this with data.zip file.

def cache_zip(zip_file_path,cache_path):
  
  
  if os.path.exists(cache_path):
    #print(os.getcwd())
    clean_cache()
    os.chdir(cache_path)
    new_cache_path = cache_path
    with zipfile.ZipFile(zip_file_path, 'r')as zip_ref:
      zip_ref.extractall(new_cache_path)
  else:
    new_cache_path = cache_path
    os.makedirs(new_cache_path)
    with zipfile.ZipFile(zip_file_path, 'r')as zip_ref:
      zip_ref.extractall(new_cache_path)
    print('no')
  #print(os.getcwd())

#cache_zip(r'C:\\Users\\OttoLanjouw\\Downloads\\winc\\files\\data.zip',r'C:\\Users\\OttoLanjouw\\Downloads\winc\\files\\cache')

# step 3
# cached_files: takes no arguments and returns a list of all the files in the cache. 
# The file paths should be specified in absolute terms. Search the web for what this means! No folders should be included in the list. 
# You do not have to account for files within folders within the cache directory.

def cached_files():
  #print(cache_path_files)
  #list_files = list(os.walk(cache_path_files))
  #print(list_files)
  #path = os.path.abspath(directory)
  file_list = []
  path = os.getcwd() + '\\cache'
  #print(path)
  #os.chdir(path)
  files = filter(lambda filepath: filepath.is_file(), Path(path).glob('*'))
  for file in files:
    file_list.append(str(file.absolute()))
  
  #print(file_list)
  return file_list
#cached_files()

# step 4
# find_password: takes the list of file paths from cached_files as an argument. 
# This function should read the text in each one to see if the password is in there. 
# Surely there should be a word in there to incidicate the presence of the password? 
# Once found, find_password should return this password string.

def find_password(file_list):
  list_directories  = os.listdir()
  
  
  list = cached_files()
  for file in list:
    with open(file) as f:
      if 'password' in f.read():
        print(f'found in {file}')
        #txt = open(file)
        #if 'password' in f:
        
        with open(str(file)) as p:
          for lines in p:
            if 'password' in lines:
              print(lines)  
        
        
        return lines
       
    
find_password(cached_files)