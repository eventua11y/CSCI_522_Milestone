function fillMaterialTable(args) -- the script format requires existence of this function
args['version']=2
args['diffuse']={1.000000, 1.000000, 1.000000, 1.000000}
args['shininess']=6.311791
args['reflectivity']={0.000000, 0.000000, 0.000000}
args['specular']={0.221311, 0.221311, 0.221311}
args['emissive']={0.000000, 0.000000, 0.000000}
args['textures']={
  {'COLOR', 'white.dds'},
} -- end textures
args['technique']='Reflected'
--notes overrides

--notes end

end
