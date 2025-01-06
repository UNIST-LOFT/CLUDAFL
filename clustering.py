import os
from typing import Dict, List
import sklearn.cluster as cluster
import argparse

def parse_vectors(path:str):
    vectors:Dict[str, List[int]]=dict()
    for file in os.listdir(path):
        if not os.path.isdir(f'{path}/{file}'):
            with open(f'{path}/{file}','r') as f:
                vectors[file]=list(map(int,f.read().strip().split(' '))) # TODO: Change to proper seperator
    
    return vectors

def save_clusters(clusters:Dict[str,int], path:str):
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
    pass

if __name__=='__main__':
    arg_parser = argparse.ArgumentParser()
    arg_parser.add_argument('vector-path', action='store', type=str, help='Path to the directory containing the vectors')
    arg_parser.add_argument('output-path', action='store', type=str, help='Path to the output file')
    arg_parser.add_argument('cluster', action='store', type=str, help='Type of cluster',choices=['k-means'])
    arg_parser.add_argument('--k', action='store', type=int, help='Number of clusters', default=5)
    args = arg_parser.parse_args()

    vectors=parse_vectors(args.vector_path)
    if args.cluster=='k-means':
        clusters=kmeans(vectors,args.k)
        save_clusters(clusters,args.output_path)