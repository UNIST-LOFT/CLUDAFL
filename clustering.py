import os
from typing import Dict, List
import sklearn.cluster as cluster
import argparse
import sbsv

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
        vectors[dfg]=int_vec
    
    return vectors

def save_clusters(clusters:Dict[str,int], path:str):
    if path == "":
        for name,cluster in clusters.items():
            print(f'{name} {cluster}')
        return
    with open(path,'w') as f:
        for name,cluster in clusters.items():
            f.write(f'{name} {cluster}\n')

"""
    Clusters
"""
def kmeans(vectors:Dict[str, List[int]], k:int=5):
    kmeans = cluster.KMeans(n_clusters=k)
    res=kmeans.fit_predict(list(vectors.values())) # res: array[int] of cluster ids
    return {name:cluster for name,cluster in zip(vectors.keys(),res)}

def bisecting_kmeans(vectors:Dict[str, List[int]], k:int=5):
    kmeans = cluster.BisectingKMeans(n_clusters=k)
    res=kmeans.fit_predict(list(vectors.values())) # res: array[int] of cluster ids
    return {name:cluster for name,cluster in zip(vectors.keys(),res)}

if __name__=='__main__':
    arg_parser = argparse.ArgumentParser()
    arg_parser.add_argument('vector_path', action='store', type=str, help='Path to the directory containing the vectors')
    arg_parser.add_argument('cluster', action='store', type=str, help='Type of cluster',choices=['kmeans','bisecting-kmeans'])
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