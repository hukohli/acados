function fig_handle = draw_cylinders(fig_handle, map)
% DRAW_CYLINDERS - plot cylindric obstacles
  
  % Draw buildings    
  NB_EDGES  = 8;
  gray_shade = 0.8;
  
  [X,Y,Z] = cylinder(map.building_width/2,NB_EDGES);
  Z = map.max_height * Z;

  nb_buildings = length(map.buildings_north);
  for i = 1:nb_buildings
      Xtrasl = X + map.buildings_north(i);
      Ytrasl = Y + map.buildings_east(i);
      C = repmat(gray_shade*ones(size(Xtrasl)),1,1,3);
      surf(Ytrasl, Xtrasl, Z, C);
      hold on;
  end
  
  map_width = map.width;
  axes_lim = [-map_width/5, map_width+map_width/5, ...
      -map_width/5, map_width+map_width/5, ...
      0, 1.2*map.max_height];
  axis(axes_lim);
  axis square;
  view(0,90);

end