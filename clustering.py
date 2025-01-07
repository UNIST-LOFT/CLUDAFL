#!/usr/bin/env python3
import os
from typing import Dict, List
import sklearn.cluster as cluster
import argparse
import sbsv
import pickle

def read_result(filename: str) -> dict:
    parser = sbsv.parser()
    parser.add_schema("[seed] [file: str] [hash: int] [dfg: int] [res: int] [time: int] [vec: str]")
    with open(filename, 'r') as f:
        result=parser.load(f)
    
    vectors=dict()
    for r in result['seed']:
        file = r["file"]
        file_hash = r["hash"]
        dfg = r["dfg"]
        res = r["res"]
        time = r["time"]
        vec_str = r["vec"]
        vec = vec_str.strip().split(",")
        int_vec=[]
        for v in vec:
            if v!='':
                int_vec.append(int(v))
        vectors[file_hash]=int_vec
    
    return vectors

def save_clusters(clusters:Dict[str,int], path:str):
    if path == "":
        for name,cluster in clusters.items():
            print(f'{name} {cluster}')
        return
    with open(path,'w') as f:
        for name,cluster in clusters.items():
            f.write(f'{name} {cluster}\n')

def save_sklearn_model(model, path:str):
    with open(path,'wb') as f:
        pickle.dump(model,f)

"""
    Clusters
"""

def kmeans(vectors:Dict[str, List[int]], k:int=5):
    if os.path.exists(f'{args.workdir}/kmeans.pkl'):
        # Load previous model and predict (do not fit)
        with open(f'{args.workdir}/kmeans.pkl','rb') as f:
            kmeans:cluster.KMeans=pickle.load(f)
        res=kmeans.predict(list(vectors.values())) # res: array[int] of cluster ids
    else:
        # Generate new model, fit and predict if previous model not exist
        kmeans = cluster.KMeans(n_clusters=k)
        res=kmeans.fit_predict(list(vectors.values())) # res: array[int] of cluster ids
        save_sklearn_model(kmeans,f'{args.workdir}/kmeans.pkl')
    return {name:cluster for name,cluster in zip(vectors.keys(),res)}

def bisecting_kmeans(vectors:Dict[str, List[int]], k:int=5):
    if os.path.exists(f'{args.workdir}/bisecting-kmeans.pkl'):
        # Load previous model and predict (do not fit)
        with open(f'{args.workdir}/bisecting-kmeans.pkl','rb') as f:
            kmeans:cluster.BisectingKMeans=pickle.load(f)
        res=kmeans.predict(list(vectors.values())) # res: array[int] of cluster ids
    else:
        # Generate new model, fit and predict if previous model not exist
        kmeans = cluster.BisectingKMeans(n_clusters=k)
        res=kmeans.fit_predict(list(vectors.values())) # res: array[int] of cluster ids
        save_sklearn_model(kmeans,f'{args.workdir}/bisecting-kmeans.pkl')
    return {name:cluster for name,cluster in zip(vectors.keys(),res)}

if __name__=='__main__':
    arg_parser = argparse.ArgumentParser()
    arg_parser.add_argument('vector_path', action='store', type=str, help='Path to the directory containing the vectors')
    arg_parser.add_argument('cluster', action='store', type=str, help='Type of cluster',choices=['kmeans','bisecting-kmeans'])
    arg_parser.add_argument('workdir', action='store', type=str, help='Path to the working directory')
    arg_parser.add_argument('-k', '--k', action='store', type=int, help='Number of clusters', default=5)
    arg_parser.add_argument('-o', '--output', action='store', type=str, help='Path to the output file', default="")
    args = arg_parser.parse_args()

    vectors=read_result(args.vector_path)

    if args.cluster=='kmeans':
        clusters=kmeans(vectors,args.k)
        save_clusters(clusters,args.output)
    elif args.cluster=='bisecting-kmeans':
        clusters=bisecting_kmeans(vectors,args.k)
        save_clusters(clusters,args.output)